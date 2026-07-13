/*
 * Hans — preferences persistence.
 *
 * Preferences live in a resource file, "Hans Preferences", in the
 * System Folder's Preferences folder ('Pref' resource 128 holds a
 * HansPrefs struct).
 */
#include "hans.h"

#define kPrefsVersion 3
#define kPrefResType  'Pref'
#define kPrefResID    128

static void PrefsDefaults(void)
{
    HansPrefs* p = &gPrefs;
    p->version = kPrefsVersion;
    BlockMoveData("\pGeneva", p->fontName, 7);
    p->fontSize = 12;
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
