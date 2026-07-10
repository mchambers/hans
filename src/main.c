/*
 * Hans — main event loop and menu dispatch.
 */
#include "hans.h"
#include <DiskInit.h>

HansPrefs gPrefs;
MainWin gMain;
Boolean gDone = false;

/* Registry mapping our windows to their per-window structs. */
#define kMaxTaggedWindows 12
static struct { WindowRef win; void* tag; } gWinTags[kMaxTaggedWindows];

static const unsigned char* kFontNames[kNumFonts] = {
    "\pGeneva", "\pNew York", "\pMonaco", "\pCharcoal",
    "\pChicago", "\pPalatino", "\pTimes", "\pCourier"
};
static const short kFontSizes[kNumSizes] = { 9, 10, 12, 14, 18 };

void SetWindowTag(WindowRef w, void* tagStruct)
{
    short i;
    for (i = 0; i < kMaxTaggedWindows; i++) {
        if (gWinTags[i].win == w || gWinTags[i].win == NULL) {
            gWinTags[i].win = tagStruct ? w : NULL;
            gWinTags[i].tag = tagStruct;
            return;
        }
    }
}

void* GetWindowTag(WindowRef w)
{
    short i;
    if (w == NULL) return NULL;
    for (i = 0; i < kMaxTaggedWindows; i++)
        if (gWinTags[i].win == w)
            return gWinTags[i].tag;
    return NULL;
}

void FatalError(ConstStr255Param msg)
{
    ParamAlert(rErrorAlert, msg, "\p");
    ExitToShell();
}

void ShowError(ConstStr255Param msg, OSStatus err)
{
    Str255 numStr;
    if (err != noErr) {
        NumToString(err, numStr);
    } else {
        numStr[0] = 0;
    }
    ParamAlert(rErrorAlert, msg, numStr);
}

/* ---------------- Layout ---------------- */

static Rect gPreviewBtnRect;    /* the "Preview" hot spot in the status bar */

static void MainLayout(Rect* libRect, Rect* edRect, Rect* statusRect)
{
    Rect port = gMain.win->portRect;
    short contentBottom = port.bottom - kStatusBarH;
    SetRect(statusRect, 0, contentBottom, port.right, port.bottom);
    if (gMain.libHidden) {
        SetRect(libRect, 0, 0, 0, 0);
        SetRect(edRect, kCollapsedLibW, 0, port.right, contentBottom);
    } else {
        SetRect(libRect, 0, 0, kLibPaneWidth - kLibScrollW, contentBottom);
        SetRect(edRect, kLibPaneWidth + 1, 0, port.right, contentBottom);
    }
}

/* The clickable collapse/reveal tab. */
static void MainTabRect(Rect* r)
{
    if (gMain.libHidden)
        SetRect(r, 0, kLibTabTop, kCollapsedLibW, kLibTabBottom);
    else
        SetRect(r, kLibPaneWidth - kLibScrollW, kLibTabTop, kLibPaneWidth, kLibTabBottom);
}

/* A small platinum tab with a chevron: points left to collapse, right to
   reveal. */
static void MainDrawTab(void)
{
    Rect tab;
    RGBColor face = { 0xDDDD, 0xDDDD, 0xDDDD };
    RGBColor edge = { 0x8888, 0x8888, 0x8888 };
    RGBColor ink  = { 0x4444, 0x4444, 0x4444 };
    RGBColor black = { 0, 0, 0 };
    short cx, cy;
    MainTabRect(&tab);

    RGBForeColor(&face); PaintRect(&tab);
    RGBForeColor(&edge); FrameRect(&tab);
    cx = (tab.left + tab.right) / 2;
    cy = (tab.top + tab.bottom) / 2;
    RGBForeColor(&ink);
    if (gMain.libHidden) {           /* chevron pointing right */
        MoveTo(cx - 2, cy - 3); LineTo(cx + 2, cy);
        MoveTo(cx + 2, cy);     LineTo(cx - 2, cy + 3);
    } else {                          /* chevron pointing left */
        MoveTo(cx + 2, cy - 3); LineTo(cx - 2, cy);
        MoveTo(cx - 2, cy);     LineTo(cx + 2, cy + 3);
    }
    RGBForeColor(&black);
}

/* The reveal gutter shown down the left edge when the library is hidden. */
static void MainDrawRevealGutter(void)
{
    Rect port = gMain.win->portRect;
    Rect gutter;
    RGBColor bg = { 0xEEEE, 0xEEEE, 0xEEEE };
    RGBColor edge = { 0x9999, 0x9999, 0x9999 };
    RGBColor black = { 0, 0, 0 };
    SetRect(&gutter, 0, 0, kCollapsedLibW, port.bottom - kStatusBarH);
    RGBForeColor(&bg); PaintRect(&gutter);
    RGBForeColor(&edge);
    MoveTo(kCollapsedLibW, gutter.top); LineTo(kCollapsedLibW, gutter.bottom);
    RGBForeColor(&black);
    MainDrawTab();
}

static void MainDrawStatusBar(void)
{
    Rect port = gMain.win->portRect;
    Rect bar;
    RGBColor bg = { 0xEEEE, 0xEEEE, 0xEEEE };
    RGBColor edge = { 0x9999, 0x9999, 0x9999 };
    RGBColor btnFace = { 0xDDDD, 0xDDDD, 0xDDDD };
    RGBColor black = { 0, 0, 0 };
    Str255 s, num;
    short bw;

    SetRect(&bar, 0, port.bottom - kStatusBarH, port.right, port.bottom);
    SetPort(gMain.win);
    RGBForeColor(&bg); PaintRect(&bar);
    RGBForeColor(&edge);
    MoveTo(bar.left, bar.top); LineTo(bar.right, bar.top);
    RGBForeColor(&black);

    TextFont(applFont); TextSize(9);

    /* word count on the left */
    NumToString(gMain.wordCount, num);
    BlockMoveData(num + 1, s + 1, num[0]); s[0] = num[0];
    { const char* w = (gMain.wordCount == 1) ? " word" : " words";
      short i = 0; while (w[i]) i++;
      if (s[0] + i <= 255) { BlockMoveData(w, s + 1 + s[0], i); s[0] += i; } }
    MoveTo(bar.left + 8, bar.top + 13);
    DrawString(s);

    /* Preview toggle on the right (clear of the grow box) */
    bw = StringWidth("\pPreview") + 16;
    SetRect(&gPreviewBtnRect, bar.right - bw - 20, bar.top + 2,
            bar.right - 20, bar.bottom - 2);
    RGBForeColor(&btnFace); PaintRect(&gPreviewBtnRect);
    RGBForeColor(&edge);    FrameRect(&gPreviewBtnRect);
    RGBForeColor(&black);
    if (!gMain.ed.hasFile) { RGBColor gray = {0x9999,0x9999,0x9999}; RGBForeColor(&gray); }
    MoveTo(gPreviewBtnRect.left + 8, gPreviewBtnRect.top + 12);
    DrawString("\pPreview");
    RGBForeColor(&black);
    DrawGrowBox(gMain.win);
}

static void MainResize(void)
{
    Rect libRect, edRect, statusRect;
    MainLayout(&libRect, &edRect, &statusRect);
    LibraryResize(&gMain, &libRect);
    EditorResize(&gMain, &edRect);
    InvalRect(&gMain.win->portRect);
}

/* Set the title bar to the current note's name (without a .md suffix). */
void MainUpdateTitle(void)
{
    Str255 t;
    if (gMain.win == NULL) return;
    if (gMain.ed.hasFile) {
        short n = gMain.ed.file.name[0];
        BlockMoveData(gMain.ed.file.name, t, n + 1);
        if (n > 3 && t[n-2] == '.'
            && (t[n-1]=='m'||t[n-1]=='M') && (t[n]=='d'||t[n]=='D'))
            t[0] = n - 3;
    } else {
        BlockMoveData("\pHans", t, 5);
    }
    SetWTitle(gMain.win, t);
}

static void ToggleLibrary(void)
{
    gMain.libHidden = !gMain.libHidden;
    SetPort(gMain.win);
    MainResize();
    /* keep the editor's caret visible after the reflow */
    if (gMain.ed.txn) TXNShowSelection(gMain.ed.txn, false);
}

static void MainCreateWindow(void)
{
    Rect bounds;
    Rect libRect, edRect;
    short screenW = qd.screenBits.bounds.right - qd.screenBits.bounds.left;
    short screenH = qd.screenBits.bounds.bottom - qd.screenBits.bounds.top;
    short w = 720, h = 480;
    if (w > screenW - 40) w = screenW - 40;
    if (h > screenH - 60) h = screenH - 60;
    SetRect(&bounds, (screenW - w) / 2, 44 + ((screenH - 44 - h) / 3), 0, 0);
    bounds.right = bounds.left + w;
    bounds.bottom = bounds.top + h;

    gMain.kind = kWinMain;
    gMain.win = NewCWindow(NULL, &bounds, "\pHans", true, zoomDocProc,
                           (WindowPtr)-1L, true, 0);
    if (gMain.win == NULL)
        FatalError("\pCould not create the main window.");
    SetWindowTag(gMain.win, &gMain);
    SetPort(gMain.win);

    {
        Rect statusRect;
        MainLayout(&libRect, &edRect, &statusRect);
    }
    LibraryCreate(&gMain, &libRect);
    EditorCreate(&gMain, &edRect);
}

static void MainUpdate(void)
{
    Rect libRect, edRect, statusRect;
    SetPort(gMain.win);
    BeginUpdate(gMain.win);
    /* Erase the whole invalid region first (BeginUpdate clips this to the
       update region, so it's cheap). Without it, seams the components below
       don't own — e.g. the border pixels around the editor frame — keep
       whatever was previously on screen. */
    EraseRect(&gMain.win->portRect);
    MainLayout(&libRect, &edRect, &statusRect);
    if (gMain.libHidden) {
        MainDrawRevealGutter();
    } else {
        LibraryDraw(&gMain);        /* pane, scrollbar, divider */
        MainDrawTab();              /* collapse tab on the divider */
    }
    TXNDraw(gMain.ed.txn, NULL);
    MainDrawStatusBar();
    EndUpdate(gMain.win);
}

/* ---------------- Menus ---------------- */

void AdjustMenus(void)
{
    MenuHandle m;
    short i;
    Str255 curFont;

    m = GetMenuHandle(mFile);
    if (m != NULL) {
        if (gMain.ed.hasFile) {
            EnableItem(m, iSave);
            EnableItem(m, iPreviewCmd);
        } else {
            DisableItem(m, iSave);
            DisableItem(m, iPreviewCmd);
        }
        if (gPrefs.hasLibrary) {
            EnableItem(m, iNewNote);
            EnableItem(m, iNewFolder);
            EnableItem(m, iRename);
        } else {
            DisableItem(m, iNewNote);
            DisableItem(m, iNewFolder);
            DisableItem(m, iRename);
        }
    }

    m = GetMenuHandle(mFormat);
    if (m != NULL) {
        BlockMoveData(gPrefs.fontName, curFont, gPrefs.fontName[0] + 1);
        for (i = 0; i < kNumFonts; i++)
            CheckItem(m, i + 1, EqualString(kFontNames[i], curFont, false, false));
        for (i = 0; i < kNumSizes; i++)
            CheckItem(m, kNumFonts + 2 + i, kFontSizes[i] == gPrefs.fontSize);
    }

    m = GetMenuHandle(mTools);
    if (m != NULL) {
        if (gMain.ed.hasFile) EnableItem(m, iCheckStyle);
        else DisableItem(m, iCheckStyle);
        if (gPrefs.llmEnabled) EnableItem(m, iAssistant);
        else DisableItem(m, iAssistant);
    }

    m = GetMenuHandle(mView);
    if (m != NULL)
        SetMenuItemText(m, iHideLibrary,
                        gMain.libHidden ? "\pShow Library" : "\pHide Library");
}

static void DoAbout(void)
{
    Alert(rAboutAlert, NULL);
}

static void DoFormatMenu(short item)
{
    if (item >= 1 && item <= kNumFonts) {
        BlockMoveData(kFontNames[item - 1], gPrefs.fontName,
                      kFontNames[item - 1][0] + 1);
    } else if (item >= kNumFonts + 2 && item < kNumFonts + 2 + kNumSizes) {
        gPrefs.fontSize = kFontSizes[item - kNumFonts - 2];
    } else {
        return;
    }
    PrefsSave();
    EditorApplyBaseFont(&gMain);
    EditorRestyleAll(&gMain);
}

static void DoMenuCommand(long menuResult)
{
    short menuID = HiWord(menuResult);
    short item = LoWord(menuResult);
    WindowRef front = FrontWindow();

    switch (menuID) {
    case mApple:
        if (item == iAbout) {
            DoAbout();
        } else {
            Str255 daName;
            GetMenuItemText(GetMenuHandle(mApple), item, daName);
            OpenDeskAcc(daName);
        }
        break;

    case mFile:
        switch (item) {
        case iNewNote:      LibraryNewNote(&gMain); break;
        case iNewFolder:    LibraryNewFolder(&gMain); break;
        case iOpenLibrary:  LibraryChooseFolder(&gMain); break;
        case iSave:         EditorSave(&gMain); break;
        case iRename:       LibraryRenameSelected(&gMain); break;
        case iPreviewCmd:   PreviewShow(&gMain); break;
        case iQuit:         gDone = true; break;
        }
        break;

    case mEdit:
        if (item == iPreferences) {
            PrefsDialog();
        } else if (front == gMain.win && gMain.ed.txn != NULL) {
            /* The editor owns Edit-menu commands when the main window is
               frontmost. Copy and Select All are always fine; editing
               commands only apply once a note is open (no editing the
               placeholder). */
            Boolean canEdit = gMain.ed.hasFile;
            switch (item) {
            case iUndo:      if (canEdit) { TXNUndo(gMain.ed.txn); gMain.ed.needRestyleAll = true; gMain.ed.dirty = true; } break;
            case iCut:       if (canEdit) { TXNCut(gMain.ed.txn); gMain.ed.needRestyleAll = true; gMain.ed.dirty = true; } break;
            case iCopy:      TXNCopy(gMain.ed.txn); break;
            case iPaste:     if (canEdit) { TXNPaste(gMain.ed.txn); gMain.ed.needRestyleAll = true; gMain.ed.dirty = true; } break;
            case iClear:     if (canEdit) { TXNClear(gMain.ed.txn); gMain.ed.needRestyleAll = true; gMain.ed.dirty = true; } break;
            case iSelectAll: TXNSetSelection(gMain.ed.txn, kTXNStartOffset, kTXNEndOffset); break;
            }
        } else {
            /* Route editing to whichever of our windows is frontmost. */
            WinTag* tag = (WinTag*)GetWindowTag(front);
            if (tag != NULL) {
                if (tag->kind == kWinChat)         ChatEditMenu(item);
                else if (tag->kind == kWinPreview) PreviewEditMenu(item);
            }
        }
        break;

    case mView:
        if (item == iHideLibrary) ToggleLibrary();
        break;

    case mFormat:
        DoFormatMenu(item);
        break;

    case mTools:
        switch (item) {
        case iCheckStyle:    StyleCheckRun(&gMain); break;
        case iEditUserWords: StyleCheckOpenUserList(&gMain); break;
        case iAssistant:     ChatShow(&gMain); break;
        }
        break;
    }
    HiliteMenu(0);
}

/* ---------------- Event dispatch ---------------- */

static void DispatchToWindow(WindowRef w, EventRecord* ev)
{
    WinTag* tag = (WinTag*)GetWindowTag(w);
    if (tag == NULL) return;
    switch (tag->kind) {
    case kWinPreview:  PreviewHandleEvent(w, ev); break;
    case kWinChat:     ChatHandleEvent(w, ev); break;
    case kWinStyle:    StyleCheckHandleEvent(w, ev); break;
    }
}

static void CloseTaggedWindow(WindowRef w)
{
    WinTag* tag = (WinTag*)GetWindowTag(w);
    if (tag == NULL) return;
    switch (tag->kind) {
    case kWinMain:     gDone = true; break;   /* closing Hans quits */
    case kWinPreview:  PreviewClose(w); break;
    case kWinChat:     ChatClose(w); break;
    case kWinStyle:    StyleCheckClose(w); break;
    }
}

static void DoMouseDown(EventRecord* ev)
{
    WindowRef w;
    short part = FindWindow(ev->where, &w);

    switch (part) {
    case inMenuBar:
        AdjustMenus();
        DoMenuCommand(MenuSelect(ev->where));
        break;

    case inSysWindow:
        SystemClick(ev, w);
        break;

    case inDrag: {
        Rect limit = qd.screenBits.bounds;
        InsetRect(&limit, 4, 4);
        DragWindow(w, ev->where, &limit);
        break;
    }

    case inGoAway:
        if (TrackGoAway(w, ev->where))
            CloseTaggedWindow(w);
        break;

    case inZoomIn:
    case inZoomOut:
        if (TrackBox(w, ev->where, part)) {
            SetPort(w);
            ZoomWindow(w, part, false);
            if (w == gMain.win) {
                MainResize();
            } else {
                InvalRect(&w->portRect);
                DispatchToWindow(w, ev);
            }
        }
        break;

    case inGrow: {
        Rect limits;
        long newSize;
        /* the main window needs room for the library pane; the accessory
           windows (chat, preview, style check) can go smaller */
        if (w == gMain.win)
            SetRect(&limits, 420, 240, qd.screenBits.bounds.right,
                    qd.screenBits.bounds.bottom);
        else
            SetRect(&limits, 280, 180, qd.screenBits.bounds.right,
                    qd.screenBits.bounds.bottom);
        newSize = GrowWindow(w, ev->where, &limits);
        if (newSize != 0) {
            SizeWindow(w, LoWord(newSize), HiWord(newSize), true);
            if (w == gMain.win) {
                MainResize();
            } else {
                SetPort(w);
                InvalRect(&w->portRect);
                DispatchToWindow(w, ev);
            }
        }
        break;
    }

    case inContent:
        if (w != FrontWindow()) {
            SelectWindow(w);
        } else if (w == gMain.win) {
            Point pt = ev->where;
            Rect port, tab;
            SetPort(gMain.win);
            GlobalToLocal(&pt);
            port = gMain.win->portRect;

            /* status bar (bottom strip): the Preview toggle */
            if (pt.v >= port.bottom - kStatusBarH) {
                if (gMain.ed.hasFile && PtInRect(pt, &gPreviewBtnRect))
                    PreviewShow(&gMain);
                break;
            }
            /* collapse / reveal tab */
            MainTabRect(&tab);
            if (PtInRect(pt, &tab)) { ToggleLibrary(); break; }
            /* library vs editor */
            if (!gMain.libHidden && pt.h < kLibPaneWidth)
                LibraryClick(&gMain, ev);
            else
                EditorClick(&gMain, ev);
        } else {
            DispatchToWindow(w, ev);
        }
        break;
    }
}

static void DoKeyDown(EventRecord* ev)
{
    char c = ev->message & charCodeMask;
    if (ev->modifiers & cmdKey) {
        if (c == 'w' || c == 'W') {
            WindowRef front = FrontWindow();
            if (front != NULL && front != gMain.win) {
                CloseTaggedWindow(front);
                return;
            }
        }
        AdjustMenus();
        DoMenuCommand(MenuKey(c));
        return;
    }
    if (FrontWindow() == gMain.win) {
        EditorKey(&gMain, ev);
    } else {
        DispatchToWindow(FrontWindow(), ev);
    }
}

static void DoActivate(EventRecord* ev, Boolean active)
{
    WindowRef w = (WindowRef)ev->message;
    if (w == gMain.win) {
        SetPort(gMain.win);
        LibraryActivate(&gMain, active);
        TXNActivate(gMain.ed.txn, gMain.ed.frameID,
                    active ? kScrollBarsAlwaysActive : kScrollBarsSyncWithFocus);
        TXNFocus(gMain.ed.txn, active);
        DrawGrowBox(gMain.win);        /* redraw to the active/inactive look */
    } else {
        DispatchToWindow(w, ev);
    }
}

static void DoUpdate(EventRecord* ev)
{
    WindowRef w = (WindowRef)ev->message;
    if (w == gMain.win) {
        MainUpdate();
    } else {
        DispatchToWindow(w, ev);
    }
}

/* Recount words for the status bar, but only once typing has settled, and
   only when the document's length has actually changed. */
static long gStatusLastSize = -1;

/* Force a recount on the next idle — used when a different note is loaded
   (whose byte size may happen to equal the previous one's). */
void MainNoteChanged(void)
{
    gStatusLastSize = -1;
}

static void StatusIdle(void)
{
    long sz;
    if (gMain.ed.txn == NULL) return;
    sz = (long)TXNDataSize(gMain.ed.txn);
    if (sz == gStatusLastSize) return;
    if ((TickCount() - gMain.ed.lastEditTicks) < 10) return;   /* debounce */
    gStatusLastSize = sz;
    gMain.wordCount = EditorCountWords(&gMain);
    MainDrawStatusBar();
}

static void DoIdle(void)
{
    if (FrontWindow() == gMain.win && gMain.ed.txn != NULL)
        TXNIdle(gMain.ed.txn);
    EditorIdle(&gMain);
    StatusIdle();
    ChatIdle();
}

static void AdjustCursor(EventRecord* ev)
{
    WindowRef front = FrontWindow();
    if (front == gMain.win && gMain.ed.txn != NULL) {
        Point pt = ev->where;
        Rect libRect, edRect, statusRect;
        SetPort(gMain.win);
        GlobalToLocal(&pt);
        MainLayout(&libRect, &edRect, &statusRect);
        if (PtInRect(pt, &edRect)) {
            TXNAdjustCursor(gMain.ed.txn, NULL);
            return;
        }
    }
    InitCursor();
}

/* ---------------- Startup ---------------- */

static void InitToolbox(void)
{
    Handle mbar;
    OSStatus err;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    RegisterAppearanceClient();

    /* Force the QuickDraw text path: it avoids a hard dependency on the
       ATSUI/Unicode components, which MLTE would otherwise prefer and which
       aren't always present on a given Mac OS 9 install. */
    err = TXNInitTextension(NULL, 0, kTXNAlwaysUseQuickDrawTextMask);
    if (err != noErr)
        FatalError("\pThe Multilingual Text Engine could not be initialized. Hans requires Mac OS 9.");

    mbar = GetNewMBar(rMenuBar);
    if (mbar == NULL)
        FatalError("\pMissing menu resources.");
    SetMenuBar(mbar);
    AppendResMenu(GetMenuHandle(mApple), 'DRVR');
    DrawMenuBar();
}

int main(void)
{
    EventRecord ev;

    InitToolbox();
    PrefsLoad();
    MainCreateWindow();
    LibraryRescan(&gMain);
    MainUpdateTitle();
    AdjustMenus();

    while (!gDone) {
        /* Advance any in-flight assistant request and let Max animate,
           every pass, so the network never blocks the UI. */
        NetPump();
        ChatPollNet();

        if (WaitNextEvent(everyEvent, &ev, 3, NULL)) {
            switch (ev.what) {
            case mouseDown:  DoMouseDown(&ev); break;
            case keyDown:
            case autoKey:    DoKeyDown(&ev); break;
            case updateEvt:  DoUpdate(&ev); break;
            case activateEvt:
                DoActivate(&ev, (ev.modifiers & activeFlag) != 0);
                break;
            case diskEvt:
                /* Offer to initialise an unreadable disk, as every good
                   citizen should. */
                if (HiWord(ev.message) != noErr) {
                    Point pt;
                    pt.h = pt.v = 120;
                    DIBadMount(pt, ev.message);
                }
                break;
            case osEvt:
                if (((ev.message >> 24) & 0xFF) == suspendResumeMessage) {
                    Boolean resumed = (ev.message & resumeFlag) != 0;
                    if (resumed) {
                        /* pick up whatever another app put on the clipboard */
                        TXNConvertFromPublicScrap();
                    } else {
                        /* publish our clipboard, and make sure the note is
                           on disk while the user is away in another app */
                        TXNConvertToPublicScrap();
                        EditorCloseFile(&gMain);
                    }
                    if (gMain.ed.txn != NULL)
                        TXNFocus(gMain.ed.txn, resumed && FrontWindow() == gMain.win);
                }
                break;
            }
        } else {
            DoIdle();
        }
        AdjustCursor(&ev);
    }

    EditorCloseFile(&gMain);   /* saves if dirty */
    PrefsSave();
    NetShutdown();             /* abort any in-flight request, close OT */
    TXNTerminateTextension();
    return 0;
}
