/*
 * Hans — the Markdown preview window.
 *
 * Preview renders the current note into a read-only text view with the
 * syntax stripped: headings become large bold lines, emphasis renders,
 * inline code is tinted, list bullets become real bullets, blockquotes
 * are indented, and horizontal rules become a drawn line of dashes. It is
 * deliberately a *basic* renderer — enough to read the shape of a piece.
 */
#include "hans.h"

typedef struct {
    short      kind;        /* kWinPreview */
    WindowRef  win;
    TXNObject  txn;
    TXNFrameID frameID;
} PreviewWin;

static PreviewWin gPreview = { 0, NULL, NULL, 0 };

/* A styled run in rendered (output) coordinates. */
typedef struct { long s, e; unsigned short flags; unsigned char head; } Run;

static Run*  gRuns = NULL;
static long  gRunCount = 0, gRunCap = 0;

static const RGBColor kColBlack = { 0, 0, 0 };
static const RGBColor kColCode  = { 0x1111, 0x6666, 0x4444 };
static const RGBColor kColLink  = { 0x1111, 0x2222, 0xAAAA };
static const RGBColor kColQuote = { 0x5555, 0x5555, 0x5555 };

static void RunsClear(void) { gRunCount = 0; }

static void RunAdd(long s, long e, unsigned short flags, unsigned char head)
{
    if (e <= s && head == 0) return;
    if (gRunCount >= gRunCap) {
        long nc = gRunCap ? gRunCap * 2 : 128;
        Run* nr = (Run*)NewPtr(nc * sizeof(Run));
        if (nr == NULL) return;
        if (gRuns) { BlockMoveData(gRuns, nr, gRunCount * sizeof(Run)); DisposePtr((Ptr)gRuns); }
        gRuns = nr; gRunCap = nc;
    }
    gRuns[gRunCount].s = s;
    gRuns[gRunCount].e = e;
    gRuns[gRunCount].flags = flags;
    gRuns[gRunCount].head = head;
    gRunCount++;
}

/* ---- Output text buffer ---- */
static Handle gOut = NULL;
static long   gOutLen = 0;

static void OutClear(void)
{
    if (gOut == NULL) gOut = NewHandle(0);
    gOutLen = 0;
}

static void OutChar(char c)
{
    if (gOut == NULL) return;
    if (GetHandleSize(gOut) < gOutLen + 1) {
        SetHandleSize(gOut, gOutLen + 256);
        if (MemError() != noErr) return;
    }
    (*gOut)[gOutLen++] = c;
}

static void OutStr(const char* s) { while (*s) OutChar(*s++); }

/* Classify a leading/inline marker span by its first source character. */
enum { kMkDrop, kMkBullet, kMkQuote };

static short ClassifyMarker(const char* line, const MDSpan* sp, long lineLen)
{
    char c = line[sp->start];
    long spanLen = sp->end - sp->start;
    if (c == '>') return kMkQuote;
    if ((c == '-' || c == '+' || (c == '*' && spanLen == 2))
        && spanLen == 2 && sp->start == 0)
        return kMkBullet;
    /* leading bullet may be indented: allow leading spaces before it */
    if ((c == '-' || c == '+' || c == '*') && spanLen == 2) {
        long i;
        Boolean onlySpaceBefore = true;
        for (i = 0; i < sp->start; i++)
            if (line[i] != ' ' && line[i] != '\t') { onlySpaceBefore = false; break; }
        if (onlySpaceBefore) return kMkBullet;
    }
    (void)lineLen;
    return kMkDrop;
}

/* Render one source line into gOut, recording style runs. */
static void RenderLine(const char* line, long len)
{
    MDSpan spans[kMaxSpans];
    unsigned char head;
    short n, i;
    long j;
    long lineOutStart = gOutLen;
    Boolean isRule = false;
    /* per-source-char classification */
    static unsigned char sflags[1024];
    static unsigned char drop[1024];
    Boolean emitBullet = false, emitQuote = false;

    if (len > 1024) len = 1024;      /* very long lines: render head only */

    if (len == 0) { OutChar('\r'); return; }

    n = MDParseLine(line, len, spans, &head);

    for (j = 0; j < len; j++) { sflags[j] = 0; drop[j] = 0; }

    for (i = 0; i < n; i++) {
        MDSpan* sp = &spans[i];
        if (sp->flags & kMDRule) { isRule = true; }
        if (sp->flags & kMDMarker) {
            short cls = ClassifyMarker(line, sp, len);
            if (cls == kMkBullet) emitBullet = true;
            else if (cls == kMkQuote) emitQuote = true;
            for (j = sp->start; j < sp->end && j < len; j++) drop[j] = 1;
        } else {
            unsigned short f = sp->flags & (kMDBold|kMDItalic|kMDCode|kMDLink);
            for (j = sp->start; j < sp->end && j < len; j++) sflags[j] |= f;
        }
    }

    if (isRule) {
        long s = gOutLen;
        for (i = 0; i < 24; i++) OutChar((char)0xD1);    /* em dash */
        RunAdd(s, gOutLen, kMDQuote, 0);                 /* tinted gray */
        OutChar('\r');
        return;
    }

    if (emitQuote) OutStr("   ");
    if (emitBullet) { OutChar((char)0xA5); OutChar(' '); OutChar(' '); }

    /* Emit kept characters, tracking contiguous style runs. */
    {
        unsigned short curFlags = 0;
        long runStart = gOutLen;
        for (j = 0; j < len; j++) {
            if (drop[j]) continue;
            if (sflags[j] != curFlags) {
                if (curFlags != 0) RunAdd(runStart, gOutLen, curFlags, 0);
                curFlags = sflags[j];
                runStart = gOutLen;
            }
            OutChar(line[j]);
        }
        if (curFlags != 0) RunAdd(runStart, gOutLen, curFlags, 0);
    }

    if (emitQuote)
        RunAdd(lineOutStart, gOutLen, kMDQuote, 0);   /* tint the whole line */
    if (head > 0)
        RunAdd(lineOutStart, gOutLen, 0, head);

    OutChar('\r');
}

static short HeadingSize(unsigned char level, short base)
{
    switch (level) {
    case 1: return base + 12;
    case 2: return base + 7;
    case 3: return base + 4;
    default: return base + 2;
    }
}

static void ApplyRun(TXNObject txn, long s, long e, Style face, short size,
                     const RGBColor* color)
{
    TXNTypeAttributes attr[3];
    ItemCount count = 0;
    if (e <= s) return;
    attr[count].tag = kTXNQDFontStyleAttribute;
    attr[count].size = kTXNQDFontStyleAttributeSize;
    attr[count].data.dataValue = face; count++;
    if (size > 0) {
        attr[count].tag = kTXNQDFontSizeAttribute;
        attr[count].size = kTXNFontSizeAttributeSize;
        attr[count].data.dataValue = ((UInt32)size) << 16; count++;
    }
    if (color) {
        attr[count].tag = kTXNQDFontColorAttribute;
        attr[count].size = kTXNQDFontColorAttributeSize;
        attr[count].data.dataPtr = (void*)color; count++;
    }
    TXNSetTypeAttributes(txn, count, attr, s, e);
}

static void SetPreviewReadOnly(Boolean readOnly)
{
    TXNControlTag tag[1];
    TXNControlData data[1];
    tag[0] = kTXNIOPrivilegesTag;
    data[0].uValue = readOnly ? kTXNReadOnly : kTXNReadWrite;
    TXNSetTXNObjectControls(gPreview.txn, false, 1, tag, data);
}

static void BuildPreview(MainWin* mw)
{
    Handle src;
    long total, lineStart, i;
    short base = gPrefs.fontSize;
    char* p;

    SetPreviewReadOnly(false);    /* allow programmatic styling */
    RunsClear();
    OutClear();

    src = EditorGetText(mw);
    if (src == NULL || gOut == NULL) {
        if (src) DisposeHandle(src);
        SetPreviewReadOnly(true);
        return;
    }
    total = GetHandleSize(src);
    HLock(src);
    p = *src;
    lineStart = 0;
    for (i = 0; i <= total; i++) {
        if (i == total || p[i] == '\r') {
            RenderLine(p + lineStart, i - lineStart);
            lineStart = i + 1;
        }
    }
    HUnlock(src);
    DisposeHandle(src);

    /* Push rendered text into the read-only view. */
    HLock(gOut);
    TXNSetData(gPreview.txn, kTXNTextData, *gOut, gOutLen,
               kTXNStartOffset, kTXNEndOffset);
    HUnlock(gOut);

    /* Base font over everything, then layer the runs. */
    {
        TXNTypeAttributes ba[2];
        ba[0].tag = kTXNQDFontFamilyIDAttribute;
        ba[0].size = kTXNQDFontFamilyIDAttributeSize;
        ba[0].data.dataValue = FontNumFromName(gPrefs.fontName);
        ba[1].tag = kTXNQDFontSizeAttribute;
        ba[1].size = kTXNFontSizeAttributeSize;
        ba[1].data.dataValue = ((UInt32)base) << 16;
        TXNSetTypeAttributes(gPreview.txn, 2, ba, kTXNStartOffset, kTXNEndOffset);
    }

    for (i = 0; i < gRunCount; i++) {
        Run* r = &gRuns[i];
        if (r->head > 0) {
            ApplyRun(gPreview.txn, r->s, r->e, bold, HeadingSize(r->head, base), &kColBlack);
        } else if (r->flags & kMDCode) {
            ApplyRun(gPreview.txn, r->s, r->e, normal, 0, &kColCode);
        } else if (r->flags & kMDLink) {
            ApplyRun(gPreview.txn, r->s, r->e, underline, 0, &kColLink);
        } else if (r->flags & kMDBold) {
            ApplyRun(gPreview.txn, r->s, r->e, bold, 0, &kColBlack);
        } else if (r->flags & kMDItalic) {
            ApplyRun(gPreview.txn, r->s, r->e, italic, 0, &kColBlack);
        } else if (r->flags & kMDQuote) {
            ApplyRun(gPreview.txn, r->s, r->e, italic, 0, &kColQuote);
        }
    }

    TXNSetSelection(gPreview.txn, kTXNStartOffset, kTXNStartOffset);
    TXNShowSelection(gPreview.txn, false);
    SetPreviewReadOnly(true);     /* lock it back down */
    TXNForceUpdate(gPreview.txn);
}

static void PreviewLayout(Rect* frame)
{
    Rect port = gPreview.win->portRect;
    SetRect(frame, port.left, port.top, port.right, port.bottom);
}

/* "Preview — <note name>" (without the .md suffix). */
static void PreviewUpdateTitle(void)
{
    Str255 t;
    BlockMoveData("\pPreview", t, 8);
    if (gMain.ed.hasFile) {
        Str255 n;
        short len = gMain.ed.file.name[0];
        BlockMoveData(gMain.ed.file.name, n, len + 1);
        if (len > 3 && n[len-2] == '.'
            && (n[len-1]=='m'||n[len-1]=='M') && (n[len]=='d'||n[len]=='D'))
            n[0] = len - 3;
        if (t[0] + 3 + n[0] <= 255) {
            t[++t[0]] = ' ';
            t[++t[0]] = (unsigned char)0xD1;   /* em dash */
            t[++t[0]] = ' ';
            BlockMoveData(n + 1, t + t[0] + 1, n[0]);
            t[0] += n[0];
        }
    }
    SetWTitle(gPreview.win, t);
}

void PreviewShow(MainWin* mw)
{
    if (gPreview.win == NULL) {
        Rect bounds;
        Rect frame;
        TXNFrameOptions opts;
        short sw = qd.screenBits.bounds.right;
        SetRect(&bounds, sw - 520, 80, sw - 40, 560);
        if (bounds.left < 60) bounds.left = 60;
        gPreview.kind = kWinPreview;
        gPreview.win = NewCWindow(NULL, &bounds, "\pPreview", true,
                                  zoomDocProc, (WindowPtr)-1L, true, 0);
        if (gPreview.win == NULL) return;
        SetWindowTag(gPreview.win, &gPreview);
        SetPort(gPreview.win);
        PreviewLayout(&frame);

        /* Created read-write so we can populate and style it; locked to
           read-only around each rebuild via kTXNIOPrivilegesTag. */
        opts = kTXNWantVScrollBarMask | kTXNAlwaysWrapAtViewEdgeMask
             | kTXNNoKeyboardSyncMask | kTXNDisableDragAndDropMask;
        if (TXNNewObject(NULL, gPreview.win, &frame, opts,
                         kTXNTextEditStyleFrameType, kTXNTextFile,
                         kTXNSystemDefaultEncoding,
                         &gPreview.txn, &gPreview.frameID, 0) != noErr) {
            DisposeWindow(gPreview.win);
            gPreview.win = NULL;
            return;
        }
        {
            TXNControlTag t[1]; TXNControlData d[1]; TXNMargins m;
            m.leftMargin = m.rightMargin = 16;
            m.topMargin = m.bottomMargin = 12;
            t[0] = kTXNMarginsTag; d[0].marginsPtr = &m;
            TXNSetTXNObjectControls(gPreview.txn, false, 1, t, d);
        }
        /* pin the frame explicitly — TXNNewObject doesn't reliably honour it */
        TXNSetFrameBounds(gPreview.txn, frame.top, frame.left,
                          frame.bottom, frame.right, gPreview.frameID);
    } else {
        SelectWindow(gPreview.win);
        SetPort(gPreview.win);
    }
    PreviewUpdateTitle();
    BuildPreview(mw);
}

/* Called when a different note is loaded: an open preview follows along
   (without stealing the front window). */
void PreviewRefreshIfOpen(MainWin* mw)
{
    if (gPreview.win == NULL) return;
    PreviewUpdateTitle();
    BuildPreview(mw);
}

/* Edit menu when the preview is frontmost: it's read-only, so only Copy
   and Select All make sense. */
void PreviewEditMenu(short item)
{
    if (gPreview.txn == NULL) return;
    switch (item) {
    case iCopy:      TXNCopy(gPreview.txn); break;
    case iSelectAll: TXNSetSelection(gPreview.txn, kTXNStartOffset, kTXNEndOffset); break;
    }
}

void PreviewHandleEvent(WindowRef w, EventRecord* ev)
{
    Rect frame;
    if (w != gPreview.win) return;

    /* Keep the text frame matched to the (possibly just-resized) window. */
    SetPort(w);
    PreviewLayout(&frame);
    TXNSetFrameBounds(gPreview.txn, frame.top, frame.left,
                      frame.bottom, frame.right, gPreview.frameID);

    switch (ev->what) {
    case updateEvt:
        BeginUpdate(w);
        EraseRect(&w->portRect);    /* clipped to the update region */
        TXNDraw(gPreview.txn, NULL);
        EndUpdate(w);
        break;
    case activateEvt: {
        Boolean act = (ev->modifiers & activeFlag) != 0;
        TXNActivate(gPreview.txn, gPreview.frameID, kScrollBarsSyncWithFocus);
        TXNFocus(gPreview.txn, act);
        break;
    }
    case mouseDown:
        TXNClick(gPreview.txn, ev);
        break;
    }
}

void PreviewClose(WindowRef w)
{
    if (w != gPreview.win) return;
    if (gPreview.txn) { TXNDeleteObject(gPreview.txn); gPreview.txn = NULL; }
    SetWindowTag(gPreview.win, NULL);
    DisposeWindow(gPreview.win);
    gPreview.win = NULL;
}
