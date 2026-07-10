/*
 * Hans — the library pane.
 *
 * The left pane is a custom-drawn file browser over the user's chosen
 * library folder: a flattened tree of folders (with disclosure triangles)
 * and Markdown/text notes, with its own scrollbar. Clicking a note opens
 * it in the editor; clicking a triangle expands or collapses a folder.
 */
#include "hans.h"

#define kRowHeight   17
#define kScrollWidth 15
#define kIndentStep  14
#define kTriSize     9
#define kIsInvisibleFlag 0x4000

/* The current library tree, flattened to visible rows. */
static LibNode* gNodes = NULL;
static short    gCount = 0;
static short    gCap = 0;

/* Root of the library. */
static short gVRef = 0;
static long  gRootDir = 0;
static Boolean gHaveRoot = false;

/* Remembered expanded folders (by dirID), so redraws keep their state. */
#define kMaxExpanded 128
static long  gExpanded[kMaxExpanded];
static short gExpandedCount = 0;

static const RGBColor kSelActive   = { 0x3333, 0x5555, 0xBBBB };
static const RGBColor kSelInactive = { 0xCCCC, 0xCCCC, 0xCCCC };
static const RGBColor kFolderFill  = { 0xEEEE, 0xCC00, 0x6666 };
static const RGBColor kFolderEdge  = { 0x9999, 0x7700, 0x3333 };
static const RGBColor kWhiteCol    = { 0xFFFF, 0xFFFF, 0xFFFF };
static const RGBColor kBlackCol    = { 0x0000, 0x0000, 0x0000 };
static const RGBColor kGrayCol     = { 0x8888, 0x8888, 0x8888 };

static Boolean gPaneActive = true;

/* ---------------- Expansion set ---------------- */

static Boolean IsExpanded(long dirID)
{
    short i;
    for (i = 0; i < gExpandedCount; i++)
        if (gExpanded[i] == dirID) return true;
    return false;
}

static void SetExpanded(long dirID, Boolean on)
{
    short i;
    for (i = 0; i < gExpandedCount; i++) {
        if (gExpanded[i] == dirID) {
            if (!on) gExpanded[i] = gExpanded[--gExpandedCount];
            return;
        }
    }
    if (on && gExpandedCount < kMaxExpanded)
        gExpanded[gExpandedCount++] = dirID;
}

/* ---------------- Node array ---------------- */

static void NodesClear(void) { gCount = 0; }

static LibNode* NodesAppend(void)
{
    if (gCount >= gCap) {
        short newCap = gCap ? gCap * 2 : 64;
        LibNode* n = (LibNode*)NewPtr((long)newCap * sizeof(LibNode));
        if (n == NULL) return NULL;
        if (gNodes) {
            BlockMoveData(gNodes, n, (long)gCount * sizeof(LibNode));
            DisposePtr((Ptr)gNodes);
        }
        gNodes = n;
        gCap = newCap;
    }
    return &gNodes[gCount++];
}

/* ---------------- Directory scanning ---------------- */

static Boolean NameHasTextExt(ConstStr255Param name)
{
    short len = name[0], i, dot = -1;
    char ext[16];
    short e = 0;
    for (i = 1; i <= len; i++) if (name[i] == '.') dot = i;
    if (dot < 0) return false;
    for (i = dot + 1; i <= len && e < 15; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        ext[e++] = c;
    }
    ext[e] = 0;
    return (ext[0]=='m'&&ext[1]=='d'&&ext[2]==0)
        || (ext[0]=='t'&&ext[1]=='x'&&ext[2]=='t'&&ext[3]==0)
        || (ext[0]=='m'&&ext[1]=='k'&&ext[2]=='d'&&ext[3]==0)
        || (ext[0]=='m'&&ext[1]=='a'&&ext[2]=='r'&&ext[3]=='k'
            &&ext[4]=='d'&&ext[5]=='o'&&ext[6]=='w'&&ext[7]=='n'&&ext[8]==0);
}

typedef struct { Str63 name; long dirID; Boolean isFolder; } Entry;

static Boolean EntryLess(const Entry* a, const Entry* b)
{
    if (a->isFolder != b->isFolder) return a->isFolder;   /* folders first */
    return RelString(a->name, b->name, false, true) < 0;
}

static void ScanDir(short vRef, long dirID, unsigned char depth);

static void EmitEntry(const Entry* e, short vRef, long parentDir, unsigned char depth)
{
    LibNode* nd = NodesAppend();
    if (nd == NULL) return;
    BlockMoveData(e->name, nd->name, e->name[0] + 1);
    nd->parID = parentDir;
    nd->dirID = e->dirID;
    nd->isFolder = e->isFolder;
    nd->depth = depth;
    nd->expanded = e->isFolder && IsExpanded(e->dirID);
    if (nd->expanded)
        ScanDir(vRef, e->dirID, depth + 1);
}

static void ScanDir(short vRef, long dirID, unsigned char depth)
{
    Entry* list = NULL;
    short n = 0, cap = 0, idx = 1, i, j;
    CInfoPBRec pb;
    Str63 name;

    if (depth > 24) return;

    for (;;) {
        OSErr err;
        pb.hFileInfo.ioNamePtr = name;
        pb.hFileInfo.ioVRefNum = vRef;
        pb.hFileInfo.ioDirID = dirID;
        pb.hFileInfo.ioFDirIndex = idx;
        err = PBGetCatInfoSync(&pb);
        if (err != noErr) break;
        idx++;

        if (pb.hFileInfo.ioFlFndrInfo.fdFlags & kIsInvisibleFlag) continue;
        if (name[0] >= 1 && name[1] == '.') continue;

        {
            Boolean isFolder = (pb.hFileInfo.ioFlAttrib & ioDirMask) != 0;
            if (!isFolder) {
                Boolean textType = pb.hFileInfo.ioFlFndrInfo.fdType == 'TEXT';
                if (!textType && !NameHasTextExt(name)) continue;
            }
            if (n >= cap) {
                short nc = cap ? cap * 2 : 32;
                Entry* ne = (Entry*)NewPtr((long)nc * sizeof(Entry));
                if (ne == NULL) break;
                if (list) { BlockMoveData(list, ne, (long)n * sizeof(Entry)); DisposePtr((Ptr)list); }
                list = ne; cap = nc;
            }
            BlockMoveData(name, list[n].name, name[0] + 1);
            list[n].isFolder = isFolder;
            list[n].dirID = isFolder ? pb.dirInfo.ioDrDirID : 0;
            n++;
        }
    }

    for (i = 1; i < n; i++) {              /* insertion sort */
        Entry key = list[i];
        j = i - 1;
        while (j >= 0 && !EntryLess(&list[j], &key)) { list[j + 1] = list[j]; j--; }
        list[j + 1] = key;
    }

    for (i = 0; i < n; i++) EmitEntry(&list[i], vRef, dirID, depth);
    if (list) DisposePtr((Ptr)list);
}

/* ---------------- Geometry ---------------- */

static void LibGeometry(MainWin* mw, Rect* textRect, Rect* scrollRect)
{
    Rect port = mw->win->portRect;
    short bottom = port.bottom - kStatusBarH;
    SetRect(textRect, 0, 0, kLibPaneWidth - kScrollWidth, bottom);
    /* scrollbar starts below the collapse tab and stops above the status bar */
    SetRect(scrollRect, kLibPaneWidth - kScrollWidth, kLibTabBottom,
            kLibPaneWidth, bottom);
}

static short VisibleRows(MainWin* mw)
{
    Rect t, s;
    LibGeometry(mw, &t, &s);
    return (t.bottom - t.top) / kRowHeight;
}

static void UpdateScrollbar(MainWin* mw)
{
    short vis = VisibleRows(mw);
    short maxTop = gCount - vis;
    if (maxTop < 0) maxTop = 0;
    if (mw->libTop > maxTop) mw->libTop = maxTop;
    if (mw->libTop < 0) mw->libTop = 0;
    if (mw->libScroll) {
        SetControlMaximum(mw->libScroll, maxTop);
        SetControlValue(mw->libScroll, mw->libTop);
        HiliteControl(mw->libScroll, maxTop > 0 ? 0 : 255);
    }
}

/* ---------------- Drawing ---------------- */

static void DrawTriangle(short x, short y, Boolean expanded)
{
    PolyHandle poly = OpenPoly();
    if (expanded) {
        MoveTo(x, y); LineTo(x + kTriSize, y);
        LineTo(x + kTriSize / 2, y + kTriSize); LineTo(x, y);
    } else {
        MoveTo(x, y); LineTo(x + kTriSize, y + kTriSize / 2);
        LineTo(x, y + kTriSize); LineTo(x, y);
    }
    ClosePoly();
    RGBForeColor(&kGrayCol);
    PaintPoly(poly);
    RGBForeColor(&kBlackCol);
    KillPoly(poly);
}

static void DrawFolderIcon(short x, short y)
{
    Rect r;
    SetRect(&r, x, y + 3, x + 14, y + 12);
    RGBForeColor(&kFolderFill); PaintRect(&r);
    SetRect(&r, x, y + 1, x + 6, y + 4);
    PaintRect(&r);
    SetRect(&r, x, y + 1, x + 14, y + 12);
    RGBForeColor(&kFolderEdge); FrameRect(&r);
    RGBForeColor(&kBlackCol);
}

static void DrawDocIcon(short x, short y)
{
    Rect r;
    SetRect(&r, x + 1, y + 1, x + 12, y + 13);
    RGBForeColor(&kWhiteCol); PaintRect(&r);
    RGBForeColor(&kGrayCol);   FrameRect(&r);
    MoveTo(x + 3, y + 4);  LineTo(x + 9, y + 4);
    MoveTo(x + 3, y + 6);  LineTo(x + 9, y + 6);
    MoveTo(x + 3, y + 8);  LineTo(x + 8, y + 8);
    RGBForeColor(&kBlackCol);
}

static void DrawRow(MainWin* mw, short row, const Rect* textRect)
{
    LibNode* nd = &gNodes[row];
    short y = textRect->top + (row - mw->libTop) * kRowHeight;
    short indent = 4 + nd->depth * kIndentStep;
    Boolean selected = (row == mw->libSel);
    Rect rowRect;
    SetRect(&rowRect, textRect->left, y, textRect->right, y + kRowHeight);

    if (selected) {
        RGBForeColor(gPaneActive ? &kSelActive : &kSelInactive);
        PaintRect(&rowRect);
        RGBForeColor(&kBlackCol);
    }

    if (nd->isFolder)
        DrawTriangle(indent, y + (kRowHeight - kTriSize) / 2, nd->expanded);
    if (nd->isFolder)
        DrawFolderIcon(indent + kTriSize + 3, y + 1);
    else
        DrawDocIcon(indent + kTriSize + 3, y + 1);

    {
        Str63 shown;
        short nameLen = nd->name[0];
        BlockMoveData(nd->name, shown, nameLen + 1);
        if (nameLen > 3 && nd->name[nameLen-2]=='.'
            && (nd->name[nameLen-1]=='m'||nd->name[nameLen-1]=='M')
            && (nd->name[nameLen]=='d'||nd->name[nameLen]=='D'))
            shown[0] = nameLen - 3;
        TextFont(applFont);
        TextSize(9);
        if (selected && gPaneActive) RGBForeColor(&kWhiteCol);
        else RGBForeColor(&kBlackCol);
        MoveTo(indent + kTriSize + 3 + 16 + 2, y + 12);
        DrawString(shown);
        RGBForeColor(&kBlackCol);
    }
}

void LibraryDraw(MainWin* mw)
{
    Rect textRect, scrollRect;
    short row, vis, last;

    if (mw->libHidden) return;
    LibGeometry(mw, &textRect, &scrollRect);
    RGBForeColor(&kWhiteCol); PaintRect(&textRect); RGBForeColor(&kBlackCol);

    TextFont(applFont); TextSize(9);
    if (!gHaveRoot) {
        RGBForeColor(&kGrayCol);
        MoveTo(textRect.left + 8, textRect.top + 40);
        DrawString("\pNo library yet.");
        MoveTo(textRect.left + 8, textRect.top + 58);
        DrawString("\pUse File > Open");
        MoveTo(textRect.left + 8, textRect.top + 72);
        DrawString("\pLibrary\311");
        RGBForeColor(&kBlackCol);
    } else if (gCount == 0) {
        RGBForeColor(&kGrayCol);
        MoveTo(textRect.left + 8, textRect.top + 40);
        DrawString("\p(empty)");
        RGBForeColor(&kBlackCol);
    } else {
        vis = VisibleRows(mw);
        last = mw->libTop + vis + 1;
        if (last > gCount) last = gCount;
        for (row = mw->libTop; row < last; row++)
            DrawRow(mw, row, &textRect);
    }

    if (mw->libScroll) {
        Draw1Control(mw->libScroll);
    }

    /* The divider between the pane and the editor. Drawn here (not only on
       the window's update) so it survives being obscured and revealed. */
    {
        Rect port = mw->win->portRect;
        RGBColor gray = { 0x9999, 0x9999, 0x9999 };
        RGBForeColor(&gray);
        MoveTo(kLibPaneWidth, port.top);
        LineTo(kLibPaneWidth, port.bottom - kStatusBarH);
        RGBForeColor(&kBlackCol);
    }
}

/* ---------------- Scrolling ---------------- */

static void RedrawPane(MainWin* mw)
{
    Rect t, s;
    if (mw->libHidden) return;
    LibGeometry(mw, &t, &s);
    RGBForeColor(&kWhiteCol); PaintRect(&t); RGBForeColor(&kBlackCol);
    LibraryDraw(mw);
}

static pascal void LibScrollAction(ControlHandle ctl, short part)
{
    MainWin* mw = &gMain;
    short delta = 0, page = VisibleRows(mw) - 1, nv, mx;
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
        if (nv < 0) nv = 0;
        if (nv > mx) nv = mx;
        SetControlValue(ctl, nv);
        mw->libTop = nv;
        RedrawPane(mw);
    }
}

static ControlActionUPP gScrollUPP = NULL;

/* ---------------- Lifecycle ---------------- */

void LibraryCreate(MainWin* mw, const Rect* frame)
{
    Rect textRect, scrollRect;
    (void)frame;
    mw->libTop = 0;
    mw->libSel = -1;
    LibGeometry(mw, &textRect, &scrollRect);
    mw->libScroll = NewControl(mw->win, &scrollRect, "\p", true, 0, 0, 0,
                               kControlScrollBarLiveProc, 0);
    if (gScrollUPP == NULL)
        gScrollUPP = NewControlActionUPP(LibScrollAction);
}

void LibraryResize(MainWin* mw, const Rect* frame)
{
    Rect textRect, scrollRect;
    (void)frame;
    if (mw->libScroll == NULL) return;
    if (mw->libHidden) { HideControl(mw->libScroll); return; }
    ShowControl(mw->libScroll);
    LibGeometry(mw, &textRect, &scrollRect);
    MoveControl(mw->libScroll, scrollRect.left, scrollRect.top);
    SizeControl(mw->libScroll, scrollRect.right - scrollRect.left,
                scrollRect.bottom - scrollRect.top);
    UpdateScrollbar(mw);
}

void LibraryActivate(MainWin* mw, Boolean active)
{
    gPaneActive = active;
    if (mw->libScroll)
        HiliteControl(mw->libScroll,
                      (active && GetControlMaximum(mw->libScroll) > 0) ? 0 : 255);
}

void LibraryRescan(MainWin* mw)
{
    FSSpec root;
    NodesClear();
    gHaveRoot = PrefsGetLibrarySpec(&root, &gRootDir);
    if (gHaveRoot) {
        gVRef = root.vRefNum;
        ScanDir(gVRef, gRootDir, 0);
    }
    if (mw->libSel >= gCount) mw->libSel = -1;
    UpdateScrollbar(mw);
}

/* ---------------- Hit testing & clicks ---------------- */

static void OpenNode(MainWin* mw, short row)
{
    LibNode* nd = &gNodes[row];
    FSSpec spec;
    if (nd->isFolder) return;
    if (FSMakeFSSpec(gVRef, nd->parID, nd->name, &spec) == noErr)
        EditorLoadFile(mw, &spec);
}

void LibraryClick(MainWin* mw, EventRecord* ev)
{
    Point pt = ev->where;
    Rect textRect, scrollRect;
    short row;

    SetPort(mw->win);
    GlobalToLocal(&pt);
    LibGeometry(mw, &textRect, &scrollRect);

    if (PtInRect(pt, &scrollRect)) {
        /* NB: use a local for FindControl — it NULLs its output when no
           enabled control is hit, which must not clobber mw->libScroll. */
        ControlHandle hit = NULL;
        short part = FindControl(pt, mw->win, &hit);
        if (hit != mw->libScroll || part == 0) return;
        if (part == kControlIndicatorPart) {
            TrackControl(hit, pt, NULL);
            mw->libTop = GetControlValue(hit);
            RedrawPane(mw);
        } else {
            TrackControl(hit, pt, (ControlActionUPP)gScrollUPP);
        }
        return;
    }

    if (!PtInRect(pt, &textRect) || gCount == 0) return;

    row = mw->libTop + (pt.v - textRect.top) / kRowHeight;
    if (row < 0 || row >= gCount) return;

    {
        LibNode* nd = &gNodes[row];
        short indent = 4 + nd->depth * kIndentStep;
        Boolean onTriangle = nd->isFolder
            && pt.h >= indent && pt.h < indent + kTriSize + 3;

        mw->libSel = row;

        if (nd->isFolder && (onTriangle || (ev->modifiers & optionKey) == 0)) {
            SetExpanded(nd->dirID, !nd->expanded);
            LibraryRescan(mw);
        } else if (!nd->isFolder) {
            OpenNode(mw, row);
        }
        RedrawPane(mw);
    }
}

/* ---------------- Commands ---------------- */

static long TargetDir(MainWin* mw)
{
    if (mw->libSel >= 0 && mw->libSel < gCount) {
        LibNode* nd = &gNodes[mw->libSel];
        return nd->isFolder ? nd->dirID : nd->parID;
    }
    return gRootDir;
}

static void SelectByName(MainWin* mw, long dir, ConstStr255Param name, Boolean open)
{
    short i;
    for (i = 0; i < gCount; i++) {
        if (gNodes[i].parID == dir
            && EqualString(gNodes[i].name, name, false, false)) {
            mw->libSel = i;
            if (open && !gNodes[i].isFolder) OpenNode(mw, i);
            return;
        }
    }
}

void LibraryNewNote(MainWin* mw)
{
    Str255 name;
    long dir;
    FSSpec spec;
    OSErr err;

    if (!gHaveRoot) { LibraryChooseFolder(mw); if (!gHaveRoot) return; }

    BlockMoveData("\pUntitled.md", name, 12);
    if (!AskForName("\pName for the new note:", name)) return;
    if (!NameHasTextExt(name)) {
        /* File Manager names cap at 31 characters — leave room for ".md" */
        if (name[0] > 28) name[0] = 28;
        BlockMoveData(".md", name + name[0] + 1, 3);
        name[0] += 3;
    }

    dir = TargetDir(mw);
    err = FSMakeFSSpec(gVRef, dir, name, &spec);
    if (err != noErr && err != fnfErr) { ShowError("\pCould not create the note.", err); return; }
    err = FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript);
    if (err != noErr && err != dupFNErr) { ShowError("\pCould not create the note.", err); return; }

    if (dir != gRootDir) SetExpanded(dir, true);
    LibraryRescan(mw);
    SelectByName(mw, dir, name, true);
    RedrawPane(mw);
}

void LibraryNewFolder(MainWin* mw)
{
    Str255 name;
    long dir, newDir;
    OSErr err;

    if (!gHaveRoot) { LibraryChooseFolder(mw); if (!gHaveRoot) return; }

    BlockMoveData("\pNew Folder", name, 11);
    if (!AskForName("\pName for the new folder:", name)) return;

    dir = TargetDir(mw);
    err = DirCreate(gVRef, dir, name, &newDir);
    if (err != noErr && err != dupFNErr) { ShowError("\pCould not create the folder.", err); return; }

    if (dir != gRootDir) SetExpanded(dir, true);
    LibraryRescan(mw);
    SelectByName(mw, dir, name, false);
    RedrawPane(mw);
}

void LibraryRenameSelected(MainWin* mw)
{
    LibNode* nd;
    Str255 name;
    FSSpec spec, newSpec;
    OSErr err;
    long parID;
    Boolean wasOpenFile, isFolder;

    if (mw->libSel < 0 || mw->libSel >= gCount) {
        ParamAlert(rErrorAlert, "\pSelect a note or folder to rename first.", "\p");
        return;
    }
    nd = &gNodes[mw->libSel];
    parID = nd->parID;
    isFolder = nd->isFolder;
    BlockMoveData(nd->name, name, nd->name[0] + 1);
    if (!AskForName("\pRename to:", name)) return;
    if (!isFolder && !NameHasTextExt(name)) {
        /* keep notes recognisable as notes; cap at 31 chars total */
        if (name[0] > 28) name[0] = 28;
        BlockMoveData(".md", name + name[0] + 1, 3);
        name[0] += 3;
    }

    err = FSMakeFSSpec(gVRef, parID, gNodes[mw->libSel].name, &spec);
    if (err != noErr) { ShowError("\pCould not find the item.", err); return; }

    wasOpenFile = mw->ed.hasFile && !isFolder
        && mw->ed.file.parID == spec.parID
        && EqualString(mw->ed.file.name, spec.name, false, false);
    if (wasOpenFile && mw->ed.dirty) EditorSave(mw);

    err = FSpRename(&spec, name);
    if (err != noErr) { ShowError("\pCould not rename the item.", err); return; }

    if (wasOpenFile && FSMakeFSSpec(gVRef, parID, name, &newSpec) == noErr) {
        mw->ed.file = newSpec;
        MainUpdateTitle();
    }

    LibraryRescan(mw);
    SelectByName(mw, parID, name, false);
    RedrawPane(mw);
}

/* ---------------- Choose library folder ---------------- */

/* Build a full colon path ("Volume:folder:sub:") for a folder given its
   own FSSpec (spec.name is the folder, spec.parID its parent). */
static void BuildFolderPath(const FSSpec* spec, Str255 outPath)
{
    Str255 comp, tmp;
    long dir;
    short vref = spec->vRefNum;

    BlockMoveData(spec->name, comp, spec->name[0] + 1);
    outPath[0] = comp[0];
    BlockMoveData(comp + 1, outPath + 1, comp[0]);
    dir = spec->parID;

    while (dir != fsRtParID) {
        CInfoPBRec pb;
        short cl, pl;
        pb.dirInfo.ioNamePtr = comp;
        pb.dirInfo.ioVRefNum = vref;
        pb.dirInfo.ioDrDirID = dir;
        pb.dirInfo.ioFDirIndex = -1;      /* info about dir itself */
        if (PBGetCatInfoSync(&pb) != noErr) break;

        cl = comp[0]; pl = outPath[0];
        if (cl + 1 + pl <= 255) {
            BlockMoveData(outPath + 1, tmp + 1, pl);      /* save current */
            BlockMoveData(comp + 1, outPath + 1, cl);     /* prefix comp */
            outPath[1 + cl] = ':';
            BlockMoveData(tmp + 1, outPath + 2 + cl, pl);
            outPath[0] = cl + 1 + pl;
        }
        dir = pb.dirInfo.ioDrParID;
    }
    if (outPath[0] < 255) outPath[++outPath[0]] = ':';    /* trailing colon */
}

void LibraryChooseFolder(MainWin* mw)
{
    NavDialogOptions options;
    NavReplyRecord reply;
    OSErr err;

    if (NavGetDefaultDialogOptions(&options) != noErr) return;
    BlockMoveData("\pChoose Library Folder", options.windowTitle, 22);
    BlockMoveData("\pHans", options.clientName, 5);

    err = NavChooseFolder(NULL, &reply, &options, NULL, NULL, NULL);
    if (err != noErr) return;
    if (!reply.validRecord) { NavDisposeReply(&reply); return; }

    {
        AEKeyword kw;
        DescType type;
        FSSpec spec;
        Size actual;
        if (AEGetNthPtr(&reply.selection, 1, typeFSS, &kw, &type,
                        &spec, sizeof(spec), &actual) == noErr) {
            Str255 path;
            BuildFolderPath(&spec, path);
            BlockMoveData(path, gPrefs.libraryPath, path[0] + 1);
            gPrefs.hasLibrary = true;
            gExpandedCount = 0;
            PrefsSave();
        }
    }
    NavDisposeReply(&reply);

    mw->libSel = -1;
    mw->libTop = 0;
    LibraryRescan(mw);
    SetPort(mw->win);
    RedrawPane(mw);
}
