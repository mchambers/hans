/*
 * Hans — a markdown writing environment for Mac OS 9.
 * Shared types, constants, and module interfaces.
 */
#ifndef HANS_H
#define HANS_H

#include <MacTypes.h>
#include <Quickdraw.h>
#include <QuickdrawText.h>
#include <Fonts.h>
#include <MacWindows.h>
#include <Menus.h>
#include <Events.h>
#include <Dialogs.h>
#include <Controls.h>
#include <ControlDefinitions.h>
#include <Appearance.h>
#include <MacTextEditor.h>
#include <Lists.h>
#include <Files.h>
#include <Folders.h>
#include <Navigation.h>
#include <Resources.h>
#include <TextUtils.h>
#include <ToolUtils.h>
#include <Script.h>
#include <Sound.h>
#include <Icons.h>
#include <MixedMode.h>
#include <Devices.h>

/* ---------- Resource IDs ---------- */
enum {
    rMenuBar        = 128,
    mApple          = 128,
    iAbout          = 1,
    mFile           = 129,
    iNewNote        = 1,
    iNewFolder      = 2,
    iOpenLibrary    = 4,
    iSave           = 6,
    iRename         = 7,
    iPreviewCmd     = 9,
    iQuit           = 11,
    mEdit           = 130,
    iUndo           = 1,
    iCut            = 3,
    iCopy           = 4,
    iPaste          = 5,
    iClear          = 6,
    iSelectAll      = 8,
    mFormat         = 131,
    /* fonts are items 1..kNumFonts, then a divider, then sizes */
    mTools          = 132,
    iCheckStyle     = 1,
    iEditUserWords  = 2,
    mView           = 133,
    iHideLibrary    = 1,

    rAboutAlert     = 128,
    rErrorAlert     = 129,
    rNameDialog     = 131,
    rConfirmAlert   = 132,

    rTextCliches    = 300,      /* 'TEXT' word lists, newline separated */
    rTextFillers    = 301,
    rTextRedundant  = 302,
    rTextHedges     = 303
};

/* Name dialog items */
enum { kNameOK = 1, kNameCancel, kNamePrompt, kNameText };

/* ---------- Window kinds ---------- */
enum { kWinMain = 1, kWinPreview, kWinStyle };

/* Every per-window struct begins with a short 'kind'; a pointer to the
   struct is stored in the window's RefCon. */
typedef struct { short kind; } WinTag;

#define kNumFonts 8
#define kNumSizes 5

/* ---------- Preferences ---------- */
typedef struct {
    short   version;
    Str63   fontName;       /* editor font */
    short   fontSize;
    Boolean hasLibrary;
    Str255  libraryPath;    /* full path to library folder, colon separated */
} HansPrefs;

extern HansPrefs gPrefs;

/* ---------- Editor ---------- */
typedef struct {
    TXNObject   txn;
    TXNFrameID  frameID;
    Rect        frameRect;          /* current frame, for object recreation */
    Boolean     hasFile;
    FSSpec      file;
    Boolean     dirty;
    Boolean     needRestyleLine;    /* restyle line(s) touching selection */
    Boolean     needRestyleAll;
    unsigned long lastEditTicks;
} Editor;

/* ---------- Main window ---------- */
#define kLibPaneWidth   176
#define kLibScrollW     15      /* library pane's own scrollbar width */
#define kStatusBarH     18      /* bottom status strip */
#define kCollapsedLibW  16      /* reveal gutter width when library is hidden */
#define kLibTabTop      3       /* collapse/reveal tab, within the top gutter */
#define kLibTabBottom   17

typedef struct {
    short         kind;
    WindowRef     win;
    ControlHandle libScroll;    /* vertical scrollbar for the library pane */
    short         libTop;       /* index of the first visible row */
    short         libSel;       /* selected row, or -1 */
    Boolean       libHidden;    /* focus mode: library collapsed */
    long          wordCount;    /* cached editor word count for the status bar */
    Editor        ed;
} MainWin;

extern MainWin gMain;
extern Boolean gDone;

/* ---------- Library model ---------- */
typedef struct {
    Str63   name;
    long    parID;          /* directory containing this item */
    long    dirID;          /* folders only: the folder's own dirID */
    Boolean isFolder;
    Boolean expanded;
    unsigned char depth;
} LibNode;

/* main.c */
void FatalError(ConstStr255Param msg);
void ShowError(ConstStr255Param msg, OSStatus err);
void AdjustMenus(void);
void MainUpdateTitle(void);     /* set the window title to the current note */
void MainNoteChanged(void);     /* force the status bar to recount words */
void SetWindowTag(WindowRef w, void* tagStruct);
void* GetWindowTag(WindowRef w);       /* NULL if none */

/* prefs.c */
void PrefsLoad(void);
void PrefsSave(void);
Boolean PrefsGetLibrarySpec(FSSpec* spec, long* dirID);   /* resolve library folder */

/* dialogutil.c */
Boolean AskForName(ConstStr255Param prompt, Str255 ioName);
void ParamAlert(short alertID, ConstStr255Param p0, ConstStr255Param p1);
void CToPStr(const char* c, Str255 p);
void PToCStr(ConstStr255Param p, char* c, long max);
void DrawGrowBox(WindowRef w);   /* grow icon clipped to the corner box */

/* markdown.c */
enum {
    kMDBold     = 0x0001,
    kMDItalic   = 0x0002,
    kMDCode     = 0x0004,
    kMDMarker   = 0x0008,   /* syntax characters: #, *, `, >, list bullets */
    kMDQuote    = 0x0010,
    kMDLink     = 0x0020,
    kMDRule     = 0x0040    /* horizontal rule line */
};

typedef struct {
    long start, end;        /* byte offsets relative to line start */
    unsigned short flags;
} MDSpan;

#define kMaxSpans 64

/* Parse one line (text without trailing CR). Returns number of spans.
   headLevel: 0 = body, 1..6 = heading level. */
short MDParseLine(const char* p, long len, MDSpan* spans, unsigned char* headLevel);

/* editor.c */
void EditorCreate(MainWin* mw, const Rect* frame);
void EditorResize(MainWin* mw, const Rect* frame);
void EditorLoadFile(MainWin* mw, const FSSpec* spec);
void EditorSave(MainWin* mw);
void EditorCloseFile(MainWin* mw);       /* saves if dirty */
void EditorKey(MainWin* mw, EventRecord* ev);
void EditorClick(MainWin* mw, EventRecord* ev);
void EditorIdle(MainWin* mw);
void EditorRestyleAll(MainWin* mw);
void EditorApplyBaseFont(MainWin* mw);
void EditorSetInsertionFont(MainWin* mw);
Handle EditorGetText(MainWin* mw);       /* caller disposes */
void EditorSelectRange(MainWin* mw, long start, long end);
long EditorCountWords(MainWin* mw);
short FontNumFromName(ConstStr255Param name);

/* library.c */
void LibraryCreate(MainWin* mw, const Rect* frame);
void LibraryResize(MainWin* mw, const Rect* frame);
void LibraryRescan(MainWin* mw);
void LibraryClick(MainWin* mw, EventRecord* ev);
void LibraryDraw(MainWin* mw);
void LibraryActivate(MainWin* mw, Boolean active);
void LibraryChooseFolder(MainWin* mw);
void LibraryNewNote(MainWin* mw);
void LibraryNewFolder(MainWin* mw);
void LibraryRenameSelected(MainWin* mw);

/* preview.c */
void PreviewShow(MainWin* mw);
void PreviewHandleEvent(WindowRef w, EventRecord* ev);
void PreviewClose(WindowRef w);
void PreviewRefreshIfOpen(MainWin* mw);  /* re-render after a note switch */
void PreviewEditMenu(short item);        /* Copy / Select All when frontmost */

/* stylecheck.c */
void StyleCheckRun(MainWin* mw);
void StyleCheckHandleEvent(WindowRef w, EventRecord* ev);
void StyleCheckClose(WindowRef w);
void StyleCheckOpenUserList(MainWin* mw);

#endif /* HANS_H */
