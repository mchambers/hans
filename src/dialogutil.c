/*
 * Hans — small dialog and string helpers.
 */
#include "hans.h"

void CToPStr(const char* c, Str255 p)
{
    long n = 0;
    while (c[n] != 0 && n < 255) n++;
    p[0] = (unsigned char)n;
    BlockMoveData(c, p + 1, n);
}

void PToCStr(ConstStr255Param p, char* c, long max)
{
    long n = p[0];
    if (n > max - 1) n = max - 1;
    BlockMoveData(p + 1, c, n);
    c[n] = 0;
}

/* Draw just the grow box in the window's bottom-right corner. DrawGrowIcon
   also draws scrollbar-frame lines up the window edges, which our custom
   layouts don't want, so clip to the 15x15 corner. */
void DrawGrowBox(WindowRef w)
{
    RgnHandle saved = NewRgn();
    Rect corner = w->portRect;
    if (saved == NULL) return;
    corner.left = corner.right - 15;
    corner.top = corner.bottom - 15;
    GetClip(saved);
    ClipRect(&corner);
    DrawGrowIcon(w);
    SetClip(saved);
    DisposeRgn(saved);
}

void ParamAlert(short alertID, ConstStr255Param p0, ConstStr255Param p1)
{
    ParamText(p0, p1, "\p", "\p");
    InitCursor();
    StopAlert(alertID, NULL);
}

/* Modal prompt for a file/folder name. Returns false on Cancel.
   ioName carries the initial value in and the result out. */
Boolean AskForName(ConstStr255Param prompt, Str255 ioName)
{
    DialogRef dlg;
    short itemHit;
    short type;
    Handle item;
    Rect box;
    Boolean ok = false;

    ParamText(prompt, "\p", "\p", "\p");
    dlg = GetNewDialog(rNameDialog, NULL, (WindowPtr)-1L);
    if (dlg == NULL) return false;

    SetDialogDefaultItem(dlg, kNameOK);
    SetDialogCancelItem(dlg, kNameCancel);

    GetDialogItem(dlg, kNameText, &type, &item, &box);
    SetDialogItemText(item, ioName);
    SelectDialogItemText(dlg, kNameText, 0, 32767);

    do {
        ModalDialog(NULL, &itemHit);
    } while (itemHit != kNameOK && itemHit != kNameCancel);

    if (itemHit == kNameOK) {
        GetDialogItemText(item, ioName);
        /* File Manager names are at most 31 characters and may not
           contain colons. */
        if (ioName[0] > 31) ioName[0] = 31;
        {
            short i;
            for (i = 1; i <= ioName[0]; i++)
                if (ioName[i] == ':') ioName[i] = '-';
        }
        ok = ioName[0] > 0;
    }
    DisposeDialog(dlg);
    return ok;
}
