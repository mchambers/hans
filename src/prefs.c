/*
 * Hans — preferences persistence and the Preferences dialog.
 *
 * Preferences live in a resource file, "Hans Preferences", in the
 * System Folder's Preferences folder ('Pref' resource 128 holds a
 * HansPrefs struct).
 */
#include "hans.h"

#define kPrefsVersion 2
#define kPrefResType  'Pref'
#define kPrefResID    128

static void PrefsDefaults(void)
{
    HansPrefs* p = &gPrefs;
    p->version = kPrefsVersion;
    p->proxyHost[0] = 0;
    p->proxyPort = 8079;
    p->apiKey[0] = 0;
    BlockMoveData("\pgpt-5-mini", p->model, 11);
    BlockMoveData("\pGeneva", p->fontName, 7);
    p->fontSize = 12;
    p->llmEnabled = true;
    p->hasLibrary = false;
    p->libraryPath[0] = 0;
}

static OSErr PrefsFileSpec(FSSpec* spec, Boolean createFolder)
{
    short vRefNum;
    long dirID;
    OSErr err = FindFolder(kOnSystemDisk, kPreferencesFolderType,
                           createFolder ? kCreateFolder : kDontCreateFolder,
                           &vRefNum, &dirID);
    if (err != noErr) return err;
    return FSMakeFSSpec(vRefNum, dirID, "\pHans Preferences", spec);
}

void PrefsLoad(void)
{
    FSSpec spec;
    short refNum;
    OSErr err;

    PrefsDefaults();

    err = PrefsFileSpec(&spec, false);
    if (err != noErr) return;   /* fnfErr: first run */

    refNum = FSpOpenResFile(&spec, fsRdPerm);
    if (refNum != -1) {
        Handle h = Get1Resource(kPrefResType, kPrefResID);
        if (h != NULL && GetHandleSize(h) == sizeof(HansPrefs)) {
            HansPrefs loaded;
            BlockMoveData(*h, &loaded, sizeof(HansPrefs));
            if (loaded.version == kPrefsVersion)
                gPrefs = loaded;
        }
        CloseResFile(refNum);
    }
}

void PrefsSave(void)
{
    FSSpec spec;
    short refNum;
    OSErr err;

    err = PrefsFileSpec(&spec, true);
    if (err != noErr && err != fnfErr) return;

    FSpCreateResFile(&spec, 'HANS', 'pref', smSystemScript);
    refNum = FSpOpenResFile(&spec, fsRdWrPerm);
    if (refNum == -1) return;

    {
        Handle h = Get1Resource(kPrefResType, kPrefResID);
        if (h != NULL) {
            RemoveResource(h);
            DisposeHandle(h);
        }
        h = NewHandle(sizeof(HansPrefs));
        if (h != NULL) {
            BlockMoveData(&gPrefs, *h, sizeof(HansPrefs));
            AddResource(h, kPrefResType, kPrefResID, "\pHans Preferences");
            WriteResource(h);
        }
    }
    CloseResFile(refNum);
}

/* Resolve the stored library path to an FSSpec of the folder plus its
   dirID. Returns false if no library is configured or it can't be found. */
Boolean PrefsGetLibrarySpec(FSSpec* spec, long* dirID)
{
    CInfoPBRec pb;
    OSErr err;

    if (!gPrefs.hasLibrary || gPrefs.libraryPath[0] == 0)
        return false;

    err = FSMakeFSSpec(0, 0, gPrefs.libraryPath, spec);
    if (err != noErr) return false;

    pb.dirInfo.ioNamePtr = spec->name;
    pb.dirInfo.ioVRefNum = spec->vRefNum;
    pb.dirInfo.ioDrDirID = spec->parID;
    pb.dirInfo.ioFDirIndex = 0;
    err = PBGetCatInfoSync(&pb);
    if (err != noErr || (pb.dirInfo.ioFlAttrib & ioDirMask) == 0)
        return false;
    *dirID = pb.dirInfo.ioDrDirID;
    return true;
}

/* ---------------- Preferences dialog ---------------- */

static void SetItemStr(DialogRef dlg, short itemNo, ConstStr255Param s)
{
    short type; Handle item; Rect box;
    GetDialogItem(dlg, itemNo, &type, &item, &box);
    SetDialogItemText(item, s);
}

static void GetItemStr(DialogRef dlg, short itemNo, Str255 s)
{
    short type; Handle item; Rect box;
    GetDialogItem(dlg, itemNo, &type, &item, &box);
    GetDialogItemText(item, s);
}

void PrefsDialog(void)
{
    DialogRef dlg;
    short itemHit;
    Str255 s;

    dlg = GetNewDialog(rPrefsDialog, NULL, (WindowPtr)-1L);
    if (dlg == NULL) return;

    SetDialogDefaultItem(dlg, kPrefsOK);
    SetDialogCancelItem(dlg, kPrefsCancel);

    SetItemStr(dlg, kPrefsHostText, gPrefs.proxyHost);
    NumToString(gPrefs.proxyPort, s);
    SetItemStr(dlg, kPrefsPortText, s);
    SetItemStr(dlg, kPrefsKeyText, gPrefs.apiKey);
    SetItemStr(dlg, kPrefsModelText, gPrefs.model);
    SelectDialogItemText(dlg, kPrefsHostText, 0, 32767);

    /* the enable-assistant checkbox */
    {
        short type; Handle item; Rect box;
        GetDialogItem(dlg, kPrefsEnableLLM, &type, &item, &box);
        SetControlValue((ControlHandle)item, gPrefs.llmEnabled ? 1 : 0);
    }

    do {
        ModalDialog(NULL, &itemHit);
        if (itemHit == kPrefsEnableLLM) {
            short type; Handle item; Rect box;
            GetDialogItem(dlg, kPrefsEnableLLM, &type, &item, &box);
            SetControlValue((ControlHandle)item, !GetControlValue((ControlHandle)item));
        }
    } while (itemHit != kPrefsOK && itemHit != kPrefsCancel);

    if (itemHit == kPrefsOK) {
        long port;
        short type; Handle item; Rect box;
        Boolean wasEnabled = gPrefs.llmEnabled;

        GetItemStr(dlg, kPrefsHostText, gPrefs.proxyHost);
        GetItemStr(dlg, kPrefsPortText, s);
        StringToNum(s, &port);
        if (port > 0 && port < 65536) gPrefs.proxyPort = port;
        GetItemStr(dlg, kPrefsKeyText, gPrefs.apiKey);
        GetItemStr(dlg, kPrefsModelText, s);
        if (s[0] > 63) s[0] = 63;
        BlockMoveData(s, gPrefs.model, s[0] + 1);

        GetDialogItem(dlg, kPrefsEnableLLM, &type, &item, &box);
        gPrefs.llmEnabled = GetControlValue((ControlHandle)item) != 0;

        PrefsSave();

        /* if the assistant was just turned off, close its window */
        if (wasEnabled && !gPrefs.llmEnabled)
            ChatCloseIfOpen();
    }
    DisposeDialog(dlg);
}
