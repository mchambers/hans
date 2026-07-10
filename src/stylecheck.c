/*
 * Hans — the style checker.
 *
 * A user-initiated pass: it scans the current note for clichés, filler
 * words, redundancies, hedges, and any phrases on the user's own list,
 * then shows every occurrence in a window.
 * Clicking a result selects it in the editor so you can weigh it in place.
 *
 * The built-in lists live in 'TEXT' resources 300..303. The user's list is
 * a plain-text file, "Hans User Words", in the Preferences folder; "Edit
 * User Word List" opens it in the editor.
 */
#include "hans.h"

#define kUserWordsFile "\pHans User Words"

/* ---- categories ---- */
enum { kCatCliche, kCatFiller, kCatRedundant, kCatHedge, kCatUser, kNumCats };
static const short kCatResID[4] = { rTextCliches, rTextFillers,
                                    rTextRedundant, rTextHedges };
static const char* kCatName[kNumCats] = {
    "Cliche", "Filler", "Redundancy", "Hedge", "Your word"
};

/* ---- phrase table (built once per run) ---- */
typedef struct { char text[64]; short len; short cat; } Phrase;
static Phrase* gPhrases = NULL;
static short   gPhraseCount = 0, gPhraseCap = 0;

/* ---- occurrences ---- */
typedef struct { long start, len; short cat; short line; } Occ;
static Occ*  gOccs = NULL;
static long  gOccCount = 0, gOccCap = 0;

/* ---- results window ---- */
#define kSRowHeight  15
#define kSHeaderH    24
#define kSScrollW    15

typedef struct {
    short         kind;         /* kWinStyle */
    WindowRef     win;
    ControlHandle scroll;
    short         top;
    short         sel;
    Str255        summary;
} StyleWin;
static StyleWin gStyle = { 0, NULL, NULL, 0, -1, "\p" };

static const RGBColor kWhite = { 0xFFFF, 0xFFFF, 0xFFFF };
static const RGBColor kBlack = { 0, 0, 0 };
static const RGBColor kGray  = { 0x7777, 0x7777, 0x7777 };
static const RGBColor kSelBg = { 0x3333, 0x5555, 0xBBBB };
static const RGBColor kCatCol[kNumCats] = {
    { 0xAAAA, 0x2222, 0x2222 },   /* cliche - red */
    { 0xBB00, 0x6600, 0x1100 },   /* filler - orange */
    { 0x8800, 0x4400, 0x9900 },   /* redundancy - purple */
    { 0x2222, 0x6666, 0x3333 },   /* hedge - green */
    { 0x2222, 0x4444, 0xAAAA }    /* user - blue */
};

/* ---------------- character helpers ---------------- */

static char LowerByte(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static Boolean IsWordByte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || (unsigned char)c >= 0x80;
}

/* ---------------- phrase table ---------------- */

static void PhraseClear(void) { gPhraseCount = 0; }

static void PhraseAdd(const char* text, short len, short cat)
{
    Phrase* p;
    if (len <= 0 || len >= 64) return;
    if (gPhraseCount >= gPhraseCap) {
        short nc = gPhraseCap ? gPhraseCap * 2 : 256;
        Phrase* np = (Phrase*)NewPtr((long)nc * sizeof(Phrase));
        if (np == NULL) return;
        if (gPhrases) { BlockMoveData(gPhrases, np, (long)gPhraseCount * sizeof(Phrase)); DisposePtr((Ptr)gPhrases); }
        gPhrases = np; gPhraseCap = nc;
    }
    p = &gPhrases[gPhraseCount++];
    { short i; for (i = 0; i < len; i++) p->text[i] = LowerByte(text[i]); }
    p->text[len] = 0;
    p->len = len;
    p->cat = cat;
}

/* Split a text blob into trimmed, lowercased phrases. */
static void LoadBlob(const char* data, long size, short cat)
{
    long i = 0, start = 0;
    for (i = 0; i <= size; i++) {
        if (i == size || data[i] == '\n' || data[i] == '\r') {
            long s = start, e = i;
            while (s < e && (data[s] == ' ' || data[s] == '\t')) s++;
            while (e > s && (data[e-1] == ' ' || data[e-1] == '\t' || data[e-1]=='\r')) e--;
            if (e > s && data[s] != '#') PhraseAdd(data + s, (short)(e - s), cat);
            start = i + 1;
        }
    }
}

static void UserWordsSpec(FSSpec* spec)
{
    short vRef; long dirID;
    FindFolder(kOnSystemDisk, kPreferencesFolderType, kDontCreateFolder,
               &vRef, &dirID);
    FSMakeFSSpec(vRef, dirID, kUserWordsFile, spec);
}

static void LoadUserWords(void)
{
    FSSpec spec;
    short refNum;
    long len = 0;
    Handle h;
    UserWordsSpec(&spec);
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) return;
    GetEOF(refNum, &len);
    h = NewHandle(len);
    if (h) {
        HLock(h);
        FSRead(refNum, &len, *h);
        LoadBlob(*h, len, kCatUser);
        HUnlock(h);
        DisposeHandle(h);
    }
    FSClose(refNum);
}

static void BuildPhrases(void)
{
    short c;
    PhraseClear();
    for (c = 0; c < 4; c++) {
        Handle h = GetResource('TEXT', kCatResID[c]);
        if (h) {
            HLock(h);
            LoadBlob(*h, GetHandleSize(h), c);
            HUnlock(h);
            ReleaseResource(h);
        }
    }
    LoadUserWords();
}

/* ---------------- occurrences ---------------- */

static void OccClear(void) { gOccCount = 0; }

static void OccAdd(long start, long len, short cat, short line)
{
    if (gOccCount >= gOccCap) {
        long nc = gOccCap ? gOccCap * 2 : 256;
        Occ* no = (Occ*)NewPtr(nc * sizeof(Occ));
        if (no == NULL) return;
        if (gOccs) { BlockMoveData(gOccs, no, gOccCount * sizeof(Occ)); DisposePtr((Ptr)gOccs); }
        gOccs = no; gOccCap = nc;
    }
    gOccs[gOccCount].start = start;
    gOccs[gOccCount].len = len;
    gOccs[gOccCount].cat = cat;
    gOccs[gOccCount].line = line;
    gOccCount++;
}

/* Does phrase p match doc at position i (case-insensitive, word-bounded)? */
static Boolean MatchAt(const char* doc, long docLen, long i, const Phrase* p)
{
    long k;
    if (i + p->len > docLen) return false;
    for (k = 0; k < p->len; k++)
        if (LowerByte(doc[i + k]) != p->text[k]) return false;
    /* boundaries: letters/digits must not butt directly against the match */
    if (IsWordByte(p->text[0]) && i > 0 && IsWordByte(doc[i - 1])) return false;
    if (IsWordByte(p->text[p->len - 1]) && i + p->len < docLen
        && IsWordByte(doc[i + p->len])) return false;
    return true;
}

static void Scan(const char* doc, long docLen)
{
    long i;
    short line = 1;
    OccClear();
    for (i = 0; i < docLen; i++) {
        if (doc[i] == '\r') { line++; continue; }
        {
            short j;
            for (j = 0; j < gPhraseCount; j++) {
                if (doc[i] == gPhrases[j].text[0]
                    || LowerByte(doc[i]) == gPhrases[j].text[0]) {
                    if (MatchAt(doc, docLen, i, &gPhrases[j])) {
                        OccAdd(i, gPhrases[j].len, gPhrases[j].cat, line);
                        break;      /* one hit per position */
                    }
                }
            }
        }
    }
}

/* ---------------- results window drawing ---------------- */

static void StyleGeom(Rect* listRect, Rect* scrollRect)
{
    Rect port = gStyle.win->portRect;
    SetRect(listRect, port.left, port.top + kSHeaderH,
            port.right - kSScrollW, port.bottom);
    /* stop above the grow box */
    SetRect(scrollRect, port.right - kSScrollW, port.top + kSHeaderH - 1,
            port.right, port.bottom - 14);
}

/* Keep the scrollbar glued to the window's right edge across resizes. */
static void StyleLayout(void)
{
    Rect lr, sr;
    if (gStyle.scroll == NULL) return;
    StyleGeom(&lr, &sr);
    MoveControl(gStyle.scroll, sr.left, sr.top);
    SizeControl(gStyle.scroll, sr.right - sr.left, sr.bottom - sr.top);
}

static short StyleVisibleRows(void)
{
    Rect l, s;
    StyleGeom(&l, &s);
    return (l.bottom - l.top) / kSRowHeight;
}

static void StyleUpdateScroll(void)
{
    short vis = StyleVisibleRows();
    short maxTop = (short)gOccCount - vis;
    if (maxTop < 0) maxTop = 0;
    if (gStyle.top > maxTop) gStyle.top = maxTop;
    if (gStyle.scroll) {
        SetControlMaximum(gStyle.scroll, maxTop);
        SetControlValue(gStyle.scroll, gStyle.top);
        HiliteControl(gStyle.scroll, maxTop > 0 ? 0 : 255);
    }
}

/* cache of the document text so the results list can show the matched words */
static Handle gDocCache = NULL;
static long   gDocCacheLen = 0;

static const char* StyleOccText(long row, short* outLen);

static void DrawStyleRow(long row, const Rect* listRect)
{
    Occ* o = &gOccs[row];
    short y = listRect->top + (short)(row - gStyle.top) * kSRowHeight;
    Rect rr;
    Str255 num;
    Boolean sel = (row == gStyle.sel);
    SetRect(&rr, listRect->left, y, listRect->right, y + kSRowHeight);
    if (sel) { RGBForeColor(&kSelBg); PaintRect(&rr); }

    TextFont(applFont); TextSize(9);

    /* line number */
    RGBForeColor(sel ? &kWhite : &kGray);
    MoveTo(listRect->left + 6, y + 11);
    NumToString(o->line, num);
    DrawString("\pL");
    DrawString(num);

    /* category dot / name */
    RGBForeColor(sel ? &kWhite : &kCatCol[o->cat]);
    MoveTo(listRect->left + 44, y + 11);
    { Str255 c; CToPStr(kCatName[o->cat], c); DrawString(c); }

    /* the phrase itself, quoted from the cached document copy */
    RGBForeColor(sel ? &kWhite : &kBlack);
    MoveTo(listRect->left + 128, y + 11);
    {
        short n; const char* t = StyleOccText(row, &n);
        Str255 ps; short i;
        ps[0] = (n > 60) ? 60 : n;
        for (i = 1; i <= ps[0]; i++) ps[i] = t[i-1];
        DrawChar('"');
        DrawString(ps);
        DrawChar('"');
    }
    RGBForeColor(&kBlack);
}

static const char* StyleOccText(long row, short* outLen)
{
    static char buf[64];
    Occ* o = &gOccs[row];
    long n = o->len; short i;
    if (n > 63) n = 63;
    if (gDocCache == NULL || o->start + n > gDocCacheLen) n = 0;
    for (i = 0; i < n; i++) buf[i] = (*gDocCache)[o->start + i];
    buf[n] = 0;
    *outLen = (short)n;
    return buf;
}

static void StyleDrawHeader(void)
{
    Rect port = gStyle.win->portRect;
    Rect hdr;
    SetRect(&hdr, port.left, port.top, port.right, port.top + kSHeaderH);
    RGBForeColor(&kWhite); PaintRect(&hdr);
    RGBForeColor(&kGray);
    MoveTo(hdr.left, hdr.bottom - 1); LineTo(hdr.right, hdr.bottom - 1);
    RGBForeColor(&kBlack);
    TextFont(applFont); TextSize(9);
    MoveTo(hdr.left + 8, hdr.top + 16);
    DrawString(gStyle.summary);
}

static void StyleDrawList(void)
{
    Rect listRect, scrollRect;
    long row, last;
    short vis;
    StyleGeom(&listRect, &scrollRect);
    RGBForeColor(&kWhite); PaintRect(&listRect); RGBForeColor(&kBlack);

    if (gOccCount == 0) {
        RGBForeColor(&kGray);
        TextFont(applFont); TextSize(9);
        MoveTo(listRect.left + 10, listRect.top + 22);
        DrawString("\pNothing flagged. Nicely done.");
        RGBForeColor(&kBlack);
    } else {
        vis = StyleVisibleRows();
        last = gStyle.top + vis + 1;
        if (last > gOccCount) last = gOccCount;
        for (row = gStyle.top; row < last; row++)
            DrawStyleRow(row, &listRect);
    }
    if (gStyle.scroll) Draw1Control(gStyle.scroll);
}

static void StyleRedraw(void)
{
    SetPort(gStyle.win);
    StyleDrawHeader();
    StyleDrawList();
}

/* ---------------- scrolling ---------------- */

static pascal void StyleScrollAction(ControlHandle ctl, short part)
{
    short delta = 0, page = StyleVisibleRows() - 1, nv, mx;
    if (page < 1) page = 1;
    switch (part) {
    case kControlUpButtonPart:   delta = -1; break;
    case kControlDownButtonPart: delta = +1; break;
    case kControlPageUpPart:     delta = -page; break;
    case kControlPageDownPart:   delta = +page; break;
    }
    if (delta) {
        nv = GetControlValue(ctl) + delta;
        mx = GetControlMaximum(ctl);
        if (nv < 0) nv = 0; if (nv > mx) nv = mx;
        SetControlValue(ctl, nv);
        gStyle.top = nv;
        StyleDrawList();
    }
}
static ControlActionUPP gStyleScrollUPP = NULL;

/* ---------------- run ---------------- */

void StyleCheckRun(MainWin* mw)
{
    Handle doc;

    /* only real notes — never the welcome placeholder */
    if (!mw->ed.hasFile) {
        ParamAlert(rErrorAlert, "\pOpen a note first, then check its style.", "\p");
        return;
    }

    BuildPhrases();

    doc = EditorGetText(mw);
    if (doc == NULL) return;

    /* keep a locked cached copy so the results list can quote matches and
       offsets stay valid while the window is open */
    if (gDocCache) { HUnlock(gDocCache); DisposeHandle(gDocCache); }
    gDocCache = doc;
    gDocCacheLen = GetHandleSize(doc);
    HLock(gDocCache);
    Scan(*gDocCache, gDocCacheLen);

    /* summary string */
    {
        Str255 num;
        gStyle.summary[0] = 0;
        NumToString(gOccCount, num);
        BlockMoveData(num + 1, gStyle.summary + 1, num[0]);
        gStyle.summary[0] = num[0];
        {
            const char* tail = (gOccCount == 1) ? " phrase to reconsider"
                                                : " phrases to reconsider";
            short tl = 0; while (tail[tl]) tl++;
            if (gStyle.summary[0] + tl <= 255) {
                BlockMoveData(tail, gStyle.summary + 1 + gStyle.summary[0], tl);
                gStyle.summary[0] += tl;
            }
        }
    }

    gStyle.top = 0;
    gStyle.sel = -1;

    if (gStyle.win == NULL) {
        Rect bounds;
        Rect sr, lr;
        SetRect(&bounds, 40, 60, 40 + 380, 60 + 320);
        gStyle.kind = kWinStyle;
        gStyle.win = NewCWindow(NULL, &bounds, "\pStyle Check", true,
                                zoomDocProc, (WindowPtr)-1L, true, 0);
        if (gStyle.win == NULL) return;
        SetWindowTag(gStyle.win, &gStyle);
        SetPort(gStyle.win);
        StyleGeom(&lr, &sr);
        gStyle.scroll = NewControl(gStyle.win, &sr, "\p", true, 0, 0, 0,
                                   kControlScrollBarLiveProc, 0);
        if (gStyleScrollUPP == NULL)
            gStyleScrollUPP = NewControlActionUPP(StyleScrollAction);
    } else {
        SelectWindow(gStyle.win);
        SetPort(gStyle.win);
    }
    StyleUpdateScroll();
    InvalRect(&gStyle.win->portRect);
    StyleRedraw();
}

void StyleCheckHandleEvent(WindowRef w, EventRecord* ev)
{
    if (w != gStyle.win) return;
    switch (ev->what) {
    case updateEvt:
        SetPort(w);
        BeginUpdate(w);
        EraseRect(&w->portRect);    /* clipped to the update region */
        StyleLayout();              /* window may have just been resized */
        StyleUpdateScroll();
        StyleDrawHeader();
        StyleDrawList();
        DrawGrowBox(w);
        EndUpdate(w);
        break;
    case activateEvt:
        StyleUpdateScroll();
        SetPort(w);
        DrawGrowBox(w);
        break;
    case mouseDown: {
        Point pt = ev->where;
        Rect listRect, scrollRect;
        SetPort(w);
        GlobalToLocal(&pt);
        StyleGeom(&listRect, &scrollRect);
        if (PtInRect(pt, &scrollRect)) {
            /* NB: local for FindControl — it NULLs its output when nothing
               enabled is hit, which must not clobber gStyle.scroll. */
            ControlHandle hit = NULL;
            short part = FindControl(pt, w, &hit);
            if (hit != gStyle.scroll || part == 0) break;
            if (part == kControlIndicatorPart) {
                TrackControl(hit, pt, NULL);
                gStyle.top = GetControlValue(hit);
                StyleDrawList();
            } else {
                TrackControl(hit, pt, (ControlActionUPP)gStyleScrollUPP);
            }
        } else if (PtInRect(pt, &listRect) && gOccCount > 0) {
            long row = gStyle.top + (pt.v - listRect.top) / kSRowHeight;
            if (row >= 0 && row < gOccCount) {
                gStyle.sel = (short)row;
                EditorSelectRange(&gMain, gOccs[row].start,
                                  gOccs[row].start + gOccs[row].len);
                StyleDrawList();
            }
        }
        break;
    }
    }
}

void StyleCheckClose(WindowRef w)
{
    if (w != gStyle.win) return;
    if (gStyle.scroll) { DisposeControl(gStyle.scroll); gStyle.scroll = NULL; }
    SetWindowTag(gStyle.win, NULL);
    DisposeWindow(gStyle.win);
    gStyle.win = NULL;
    if (gDocCache) { HUnlock(gDocCache); DisposeHandle(gDocCache); gDocCache = NULL; }
}

/* ---------------- user word list ---------------- */

void StyleCheckOpenUserList(MainWin* mw)
{
    FSSpec spec;
    short refNum;
    UserWordsSpec(&spec);

    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) {
        /* create with a friendly header the first time */
        const char* seed =
            "# Your words\n"
            "# One word or phrase per line. Hans flags these in Style Check.\n"
            "# Lines starting with # are ignored.\n";
        long len = 0; const char* q = seed; while (*q) { len++; q++; }
        if (FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript) == noErr
            && FSpOpenDF(&spec, fsRdWrPerm, &refNum) == noErr) {
            FSWrite(refNum, &len, seed);
            FSClose(refNum);
        }
    } else {
        FSClose(refNum);
    }

    /* Bring the editor forward — the list opens as an ordinary note there,
       and it would otherwise load behind the Style Check window. */
    SelectWindow(mw->win);
    SetPort(mw->win);
    EditorLoadFile(mw, &spec);
}
