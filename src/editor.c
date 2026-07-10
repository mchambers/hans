/*
 * Hans — the editor pane.
 *
 * A single MLTE (Multilingual Text Engine) object holds the note as plain
 * text. The user only ever edits plain text; Hans restyles the visible
 * markup in place a short moment after typing stops, so headings grow,
 * emphasis renders, and the syntax characters recede.
 *
 * Line endings: on disk notes use LF (portable Markdown); inside MLTE the
 * paragraph separator is CR, so we convert on load and save.
 */
#include "hans.h"

#define kRestyleDelay 24        /* ticks of quiet before we restyle (~0.4s) */
#define kAutosaveDelay 600      /* ticks of quiet before we autosave (~10s) */

/* Held after a failed autosave so its error alert doesn't re-fire on every
   idle pass; typing again re-arms it. */
static Boolean gAutosaveHeld = false;
#define kEditorMargin 8
/* Extra right-side inset: keeps word wrap comfortably clear of the vertical
   scrollbar so MLTE never has to scroll horizontally to keep the caret in
   view (there is no horizontal scrollbar to recover from that). */
#define kEditorRightMargin 26

/* Palette for inline markdown. */
static const RGBColor kColBlack  = { 0x0000, 0x0000, 0x0000 };
static const RGBColor kColMarker = { 0xAAAA, 0xAAAA, 0xAAAA };  /* dimmed syntax */
static const RGBColor kColCode   = { 0x1111, 0x6666, 0x4444 };  /* green */
static const RGBColor kColLink   = { 0x1111, 0x2222, 0xAAAA };  /* blue */
static const RGBColor kColQuote  = { 0x6666, 0x6666, 0x6666 };  /* gray */

short FontNumFromName(ConstStr255Param name)
{
    short num = 0;
    GetFNum(name, &num);
    return num;
}

static short HeadingSize(unsigned char level, short base)
{
    switch (level) {
    case 1: return base + 10;
    case 2: return base + 6;
    case 3: return base + 3;
    default: return base + 1;
    }
}

/* Apply a run of attributes over [start,end): face, optional size (0 = keep
   the document's base size), and optional color (NULL = keep black). */
static void ApplyRun(TXNObject txn, long start, long end,
                     Style face, short size, const RGBColor* color)
{
    TXNTypeAttributes attr[3];
    ItemCount count = 0;

    if (end <= start) return;

    attr[count].tag = kTXNQDFontStyleAttribute;
    attr[count].size = kTXNQDFontStyleAttributeSize;
    attr[count].data.dataValue = face;
    count++;

    if (size > 0) {
        /* MLTE font sizes are Fixed (points << 16), not the obsolete QD
           SInt16 form. */
        attr[count].tag = kTXNQDFontSizeAttribute;
        attr[count].size = kTXNFontSizeAttributeSize;
        attr[count].data.dataValue = ((UInt32)size) << 16;
        count++;
    }

    if (color != NULL) {
        attr[count].tag = kTXNQDFontColorAttribute;
        attr[count].size = kTXNQDFontColorAttributeSize;
        attr[count].data.dataPtr = (void*)color;
        count++;
    }

    TXNSetTypeAttributes(txn, count, attr, start, end);
}

/* Style one logical line whose text begins at document offset lineStart. */
static void StyleLine(TXNObject txn, const char* p, long len, long lineStart)
{
    MDSpan spans[kMaxSpans];
    unsigned char head;
    short n, i;
    short base = gPrefs.fontSize;

    if (len <= 0) return;

    n = MDParseLine(p, len, spans, &head);

    if (head > 0) {
        /* Whole heading line is bold and larger. */
        ApplyRun(txn, lineStart, lineStart + len, bold, HeadingSize(head, base),
                 &kColBlack);
    }

    for (i = 0; i < n; i++) {
        long s = lineStart + spans[i].start;
        long e = lineStart + spans[i].end;
        unsigned short f = spans[i].flags;

        if (f & kMDRule) {
            ApplyRun(txn, s, e, normal, base, &kColMarker);
        } else if (f & kMDMarker) {
            /* Keep whatever size the surrounding text has; just dim it. */
            ApplyRun(txn, s, e, (head > 0) ? bold : normal, 0, &kColMarker);
        } else if (f & kMDCode) {
            ApplyRun(txn, s, e, normal, base, &kColCode);
        } else if (f & kMDLink) {
            ApplyRun(txn, s, e, underline, base, &kColLink);
        } else if (f & kMDBold) {
            ApplyRun(txn, s, e, bold, 0, &kColBlack);
        } else if (f & kMDItalic) {
            ApplyRun(txn, s, e, italic, 0, &kColBlack);
        } else if (f & kMDQuote) {
            ApplyRun(txn, s, e, italic, 0, &kColQuote);
        }
    }
}

static void BaseFontAttrs(TXNTypeAttributes attr[2])
{
    attr[0].tag = kTXNQDFontFamilyIDAttribute;
    attr[0].size = kTXNQDFontFamilyIDAttributeSize;
    attr[0].data.dataValue = FontNumFromName(gPrefs.fontName);
    attr[1].tag = kTXNQDFontSizeAttribute;
    attr[1].size = kTXNFontSizeAttributeSize;
    attr[1].data.dataValue = ((UInt32)gPrefs.fontSize) << 16;
}

void EditorApplyBaseFont(MainWin* mw)
{
    TXNTypeAttributes attr[2];
    if (mw->ed.txn == NULL) return;
    BaseFontAttrs(attr);
    TXNSetTypeAttributes(mw->ed.txn, 2, attr, kTXNStartOffset, kTXNEndOffset);
}

/* Reset the current insertion point to the base body style — base font and
   size, normal weight, black — so the *next* character typed starts clean.
   Used when opening a note (a fresh empty note would otherwise type in
   MLTE's default font) and after Return (a new line following a heading
   would otherwise carry the heading's larger, bold style). */
void EditorSetInsertionFont(MainWin* mw)
{
    TXNTypeAttributes attr[4];
    TXNOffset s, e;
    if (mw->ed.txn == NULL) return;
    BaseFontAttrs(attr);                     /* [0] family, [1] size */
    attr[2].tag = kTXNQDFontStyleAttribute;
    attr[2].size = kTXNQDFontStyleAttributeSize;
    attr[2].data.dataValue = normal;
    attr[3].tag = kTXNQDFontColorAttribute;
    attr[3].size = kTXNQDFontColorAttributeSize;
    attr[3].data.dataPtr = (void*)&kColBlack;
    TXNGetSelection(mw->ed.txn, &s, &e);
    TXNSetTypeAttributes(mw->ed.txn, 4, attr, s, e);
}

void EditorRestyleAll(MainWin* mw)
{
    TXNObject txn = mw->ed.txn;
    Handle h;
    long total, lineStart, i;
    TXNOffset selStart, selEnd;
    char* base;

    if (txn == NULL) return;
    total = (long)TXNDataSize(txn);
    if (total <= 0) { mw->ed.needRestyleAll = false; return; }

    /* Reset the whole document to the base look, then layer markup on top. */
    ApplyRun(txn, kTXNStartOffset, kTXNEndOffset, normal, gPrefs.fontSize,
             &kColBlack);

    /* NB: TXNGetData needs real absolute offsets — passing kTXNEndOffset
       makes it fail on OS 9, which used to silently kill all restyling.
       EditorGetText fetches with TXNDataSize()-derived offsets. */
    h = EditorGetText(mw);
    if (h == NULL)
        { mw->ed.needRestyleAll = false; return; }

    TXNGetSelection(txn, &selStart, &selEnd);

    HLock(h);
    base = *h;
    total = GetHandleSize(h);
    lineStart = 0;
    for (i = 0; i <= total; i++) {
        if (i == total || base[i] == '\r') {
            StyleLine(txn, base + lineStart, i - lineStart, lineStart);
            lineStart = i + 1;
        }
    }
    HUnlock(h);
    DisposeHandle(h);

    TXNSetSelection(txn, selStart, selEnd);
    mw->ed.needRestyleAll = false;
    mw->ed.needRestyleLine = false;
}

/* Restyle only the paragraph containing the caret — plus the one before it,
   so pressing Return mid-line doesn't leave the split-off half stale. This
   is what runs after ordinary typing; the rest of the document never
   flashes. */
void EditorRestyleLine(MainWin* mw)
{
    TXNObject txn = mw->ed.txn;
    Handle h;
    long total, pos, lineStart, lineEnd, prevStart;
    TXNOffset selStart, selEnd;
    char* p;

    if (txn == NULL) { return; }
    /* real offsets only — see EditorRestyleAll */
    h = EditorGetText(mw);
    if (h == NULL) {
        mw->ed.needRestyleLine = false;
        return;
    }
    total = GetHandleSize(h);
    TXNGetSelection(txn, &selStart, &selEnd);

    HLock(h);
    p = *h;
    pos = selStart;
    if (pos > total) pos = total;
    lineStart = pos;
    while (lineStart > 0 && p[lineStart - 1] != '\r') lineStart--;
    lineEnd = pos;
    while (lineEnd < total && p[lineEnd] != '\r') lineEnd++;
    prevStart = lineStart;
    if (lineStart > 0) {
        prevStart = lineStart - 1;      /* skip the CR */
        while (prevStart > 0 && p[prevStart - 1] != '\r') prevStart--;
    }

    /* clear stale styling on the affected lines, then re-apply */
    ApplyRun(txn, prevStart, lineEnd, normal, gPrefs.fontSize, &kColBlack);
    if (prevStart < lineStart)
        StyleLine(txn, p + prevStart, lineStart - 1 - prevStart, prevStart);
    StyleLine(txn, p + lineStart, lineEnd - lineStart, lineStart);

    HUnlock(h);
    DisposeHandle(h);

    TXNSetSelection(txn, selStart, selEnd);
    mw->ed.needRestyleLine = false;
}

/* Fill the editor with a hint shown until a note is open. Typing into it is
   blocked in EditorKey (and the Edit menu), so there's no document with
   nowhere to be saved. (We deliberately avoid MLTE's read-only control,
   which is unreliable here and blocks the styling/clear the loader needs.) */
static void EditorShowPlaceholder(MainWin* mw)
{
    static const char* hint =
        "Welcome to Hans.\r\r"
        "Open a note from the library on the left, or choose New Note from "
        "the File menu, to begin writing.";
    long n = 0; const char* p = hint;
    while (*p) { n++; p++; }

    TXNSetSelection(mw->ed.txn, kTXNStartOffset, kTXNEndOffset);
    TXNClear(mw->ed.txn);
    TXNSetData(mw->ed.txn, kTXNTextData, (void*)hint, n,
               kTXNStartOffset, kTXNStartOffset);
    ApplyRun(mw->ed.txn, kTXNStartOffset, kTXNEndOffset, italic,
             gPrefs.fontSize, &kColQuote);
    TXNSetSelection(mw->ed.txn, kTXNStartOffset, kTXNStartOffset);
}

/* Create a fresh TXN object in the current frame. Hans uses one object per
   document (recreated on every load): MLTE's undo stack lives in the object
   and Universal Interfaces 3.4 has no TXNClearUndo, so reusing one object
   across notes would let Cmd-Z resurrect the previous note's text into the
   new one — which the autosave would then write over the new note's file. */
static OSStatus EditorNewObject(MainWin* mw)
{
    OSStatus err;
    TXNFrameOptions opts;
    TXNControlTag ctlTag[1];
    TXNControlData ctlData[1];
    TXNMargins margins;
    Rect frame = mw->ed.frameRect;

    opts = kTXNWantVScrollBarMask | kTXNAlwaysWrapAtViewEdgeMask
         | kTXNNoKeyboardSyncMask | kTXNDisableDragAndDropMask;

    mw->ed.txn = NULL;
    err = TXNNewObject(NULL, mw->win, &frame, opts,
                       kTXNTextEditStyleFrameType, kTXNTextFile,
                       kTXNSystemDefaultEncoding,
                       &mw->ed.txn, &mw->ed.frameID, 0);
    if (err != noErr) {
        /* Fall back to the simplest possible frame in case a particular
           option combination is unsupported on this system. */
        mw->ed.txn = NULL;
        err = TXNNewObject(NULL, mw->win, &frame, kTXNWantVScrollBarMask,
                           kTXNTextEditStyleFrameType, kTXNTextFile,
                           kTXNSystemDefaultEncoding,
                           &mw->ed.txn, &mw->ed.frameID, 0);
    }
    if (err != noErr || mw->ed.txn == NULL)
        return err ? err : -1;

    /* A comfortable margin inside the text frame. */
    margins.leftMargin = kEditorMargin;
    margins.topMargin = kEditorMargin;
    margins.rightMargin = kEditorRightMargin;
    margins.bottomMargin = kEditorMargin;
    ctlTag[0] = kTXNMarginsTag;
    ctlData[0].marginsPtr = &margins;
    TXNSetTXNObjectControls(mw->ed.txn, false, 1, ctlTag, ctlData);

    /* TXNNewObject doesn't reliably honour the initial frame rectangle, so
       set it explicitly (as a resize does). */
    TXNSetFrameBounds(mw->ed.txn, frame.top, frame.left,
                      frame.bottom, frame.right, mw->ed.frameID);

    /* If our window is already frontmost, the activate event that normally
       wires up focus has come and gone — do it by hand. */
    if (FrontWindow() == mw->win) {
        TXNActivate(mw->ed.txn, mw->ed.frameID, kScrollBarsAlwaysActive);
        TXNFocus(mw->ed.txn, true);
    }

    EditorApplyBaseFont(mw);
    return noErr;
}

void EditorCreate(MainWin* mw, const Rect* frame)
{
    OSStatus err;

    mw->ed.frameRect = *frame;
    err = EditorNewObject(mw);
    if (err != noErr) {
        Str255 code;
        NumToString(err, code);
        ParamAlert(rErrorAlert, "\pThe editor could not be created. MLTE error", code);
        ExitToShell();
        return;
    }

    mw->ed.hasFile = false;
    mw->ed.dirty = false;
    mw->ed.needRestyleAll = false;
    mw->ed.needRestyleLine = false;
    mw->ed.lastEditTicks = 0;

    EditorShowPlaceholder(mw);       /* hint shown until a note is open */
}

void EditorResize(MainWin* mw, const Rect* frame)
{
    if (mw->ed.txn == NULL) return;
    mw->ed.frameRect = *frame;
    TXNSetFrameBounds(mw->ed.txn, frame->top, frame->left,
                      frame->bottom, frame->right, mw->ed.frameID);
}

void EditorSelectRange(MainWin* mw, long start, long end)
{
    if (mw->ed.txn == NULL) return;
    TXNSetSelection(mw->ed.txn, start, end);
    TXNShowSelection(mw->ed.txn, false);
}

Handle EditorGetText(MainWin* mw)
{
    Handle h = NULL;
    ByteCount total;
    if (mw->ed.txn == NULL) return NULL;
    total = TXNDataSize(mw->ed.txn);
    if (total == 0) return NewHandle(0);
    if (TXNGetData(mw->ed.txn, kTXNStartOffset, total, &h) != noErr)
        return NULL;
    return h;
}

long EditorCountWords(MainWin* mw)
{
    Handle h;
    long n, i, count = 0;
    Boolean inWord = false;
    char* p;

    if (!mw->ed.hasFile) return 0;   /* don't count the placeholder hint */
    h = EditorGetText(mw);
    if (h == NULL) return 0;
    n = GetHandleSize(h);
    HLock(h);
    p = *h;
    for (i = 0; i < n; i++) {
        Boolean isWord = (unsigned char)p[i] > ' ';
        if (isWord && !inWord) { count++; inWord = true; }
        else if (!isWord) inWord = false;
    }
    HUnlock(h);
    DisposeHandle(h);
    return count;
}

/* Replace all in-place characters of one kind throughout a buffer. */
static void ReplaceChar(char* p, long len, char from, char to)
{
    long i;
    for (i = 0; i < len; i++)
        if (p[i] == from) p[i] = to;
}

void EditorLoadFile(MainWin* mw, const FSSpec* spec)
{
    short refNum;
    long len = 0;
    Handle data;
    OSErr err;

    EditorCloseFile(mw);        /* save any current note first */

    err = FSpOpenDF(spec, fsRdPerm, &refNum);
    if (err != noErr) { ShowError("\pCould not open the note.", err); return; }

    GetEOF(refNum, &len);
    data = NewHandle(len);
    if (data == NULL) {
        FSClose(refNum);
        ShowError("\pNot enough memory to open the note.", memFullErr);
        return;
    }
    HLock(data);
    err = FSRead(refNum, &len, *data);
    FSClose(refNum);
    if (err != noErr && err != eofErr) {
        DisposeHandle(data);
        ShowError("\pCould not read the note.", err);
        return;
    }

    /* Normalise CRLF/LF to CR for MLTE. Do CRLF first (drop the LF). */
    {
        char* src = *data;
        char* dst = *data;
        long i;
        for (i = 0; i < len; i++) {
            char c = src[i];
            if (c == '\r' && i + 1 < len && src[i + 1] == '\n') { *dst++ = '\r'; i++; }
            else if (c == '\n') *dst++ = '\r';
            else *dst++ = c;
        }
        len = dst - *data;
    }

    /* Start each note in a brand-new TXN object (fresh undo stack, fresh
       typing attributes) and insert into the empty document — see
       EditorNewObject for why we never reuse the object across notes. */
    if (mw->ed.txn != NULL) TXNDeleteObject(mw->ed.txn);
    if (EditorNewObject(mw) != noErr) {
        DisposeHandle(data);
        mw->ed.hasFile = false;
        FatalError("\pThe editor could not be re-created.");
        return;
    }
    TXNSetData(mw->ed.txn, kTXNTextData, *data, len,
               kTXNStartOffset, kTXNStartOffset);
    HUnlock(data);
    DisposeHandle(data);

    mw->ed.file = *spec;
    mw->ed.hasFile = true;
    mw->ed.dirty = false;

    EditorApplyBaseFont(mw);
    EditorRestyleAll(mw);
    TXNSetSelection(mw->ed.txn, kTXNStartOffset, kTXNStartOffset);
    EditorSetInsertionFont(mw);      /* type in the right font from char one */
    TXNShowSelection(mw->ed.txn, false);

    /* Repaint the editor now. TXNForceUpdate only recalculates layout and
       doesn't reliably post a window update event, so draw the frame
       explicitly — otherwise the new note's text doesn't appear until some
       later event (e.g. a resize) forces a redraw. */
    SetPort(mw->win);
    TXNForceUpdate(mw->ed.txn);
    TXNDraw(mw->ed.txn, NULL);

    /* Belt and braces: run one more full restyle from the idle loop after
       the object's first real draw, in case MLTE dropped any attribute
       changes made before its initial layout. Idempotent and invisible. */
    mw->ed.needRestyleAll = true;
    mw->ed.needRestyleLine = false;
    mw->ed.lastEditTicks = 0;

    MainUpdateTitle();               /* reflect the newly-opened note */
    MainNoteChanged();               /* recount words for the status bar */
    PreviewRefreshIfOpen(mw);        /* keep an open preview in step */
}

void EditorSave(MainWin* mw)
{
    short refNum;
    Handle h;
    long len;
    OSErr err;

    if (!mw->ed.hasFile || mw->ed.txn == NULL) return;

    h = EditorGetText(mw);
    if (h == NULL) return;
    len = GetHandleSize(h);

    HLock(h);
    ReplaceChar(*h, len, '\r', '\n');       /* CR -> LF for portability */

    err = FSpOpenDF(&mw->ed.file, fsRdWrPerm, &refNum);
    if (err == fnfErr) {
        err = FSpCreate(&mw->ed.file, 'ttxt', 'TEXT', smSystemScript);
        if (err == noErr) err = FSpOpenDF(&mw->ed.file, fsRdWrPerm, &refNum);
    }
    if (err != noErr) {
        HUnlock(h); DisposeHandle(h);
        ShowError("\pCould not save the note.", err);
        return;
    }

    SetEOF(refNum, 0);
    err = FSWrite(refNum, &len, *h);
    FSClose(refNum);
    HUnlock(h);
    DisposeHandle(h);

    if (err == noErr) {
        mw->ed.dirty = false;
        FlushVol(NULL, mw->ed.file.vRefNum);   /* really on disk, not in cache */
    } else {
        ShowError("\pCould not write the note.", err);
    }
}

void EditorCloseFile(MainWin* mw)
{
    if (mw->ed.hasFile && mw->ed.dirty)
        EditorSave(mw);
    mw->ed.dirty = false;
}

/* Keys that move the caret without changing the text. */
static Boolean IsNavigationKey(char c)
{
    return (c >= 0x1C && c <= 0x1F)      /* arrows */
        || c == 0x01 || c == 0x04        /* home, end */
        || c == 0x0B || c == 0x0C        /* page up, page down */
        || c == 0x1B || c == 0x05        /* escape, help */
        || c == 0x10;                    /* function keys */
}

void EditorKey(MainWin* mw, EventRecord* ev)
{
    char c = ev->message & charCodeMask;
    if (mw->ed.txn == NULL) return;
    if (!mw->ed.hasFile) return;    /* nothing open — the editor is read-only */
    TXNKeyDown(mw->ed.txn, ev);
    if (IsNavigationKey(c)) return; /* moving around isn't an edit */
    /* A new line starts fresh at the base body style, so a line after a
       heading doesn't keep the heading's larger, bold type. */
    if (c == '\r' || c == 0x03)
        EditorSetInsertionFont(mw);
    mw->ed.dirty = true;
    gAutosaveHeld = false;              /* new edits re-arm autosave */
    mw->ed.needRestyleLine = true;      /* typing restyles only the caret line */
    mw->ed.lastEditTicks = TickCount();
}

void EditorClick(MainWin* mw, EventRecord* ev)
{
    if (mw->ed.txn == NULL) return;
    TXNClick(mw->ed.txn, ev);
}

void EditorIdle(MainWin* mw)
{
    unsigned long quiet;
    if (mw->ed.txn == NULL) return;
    quiet = TickCount() - mw->ed.lastEditTicks;
    if (quiet <= kRestyleDelay) return;
    if (mw->ed.needRestyleAll)
        EditorRestyleAll(mw);           /* after load / paste / cut */
    else if (mw->ed.needRestyleLine)
        EditorRestyleLine(mw);          /* after ordinary typing */
    /* Autosave once typing has been quiet for a while; dirty resets on
       save, so this writes once per editing burst. */
    else if (mw->ed.dirty && mw->ed.hasFile && !gAutosaveHeld
             && quiet > kAutosaveDelay) {
        EditorSave(mw);
        if (mw->ed.dirty) gAutosaveHeld = true;   /* save failed */
    }
}
