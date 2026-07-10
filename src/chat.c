/*
 * Hans — the writing assistant ("Max").
 *
 * A modeless chat window with a little animated Mac Plus character in the
 * header, a read-only transcript, an input box, and a Send button. Each
 * message is posted to the Hans Proxy along with the current note as
 * context; the proxy calls the OpenAI Conversations API and returns the
 * reply as plain text.
 *
 * The request is fully asynchronous: NetStart() kicks it off, the main
 * event loop calls ChatPollNet()/NetPump() every pass, and Max animates a
 * "thinking" face until the reply lands. Nothing blocks.
 */
#include "hans.h"
#include <string.h>

#define kChatHeaderH  46
#define kChatInputH   64
#define kChatBtnW     72
#define kChatBtnH     20
#define kChatGap      8
#define kMaxDocChars  8000L

/* Max's moods. */
enum { kMoodIdle = 0, kMoodThinking, kMoodHappy, kMoodOops };

typedef struct {
    short         kind;         /* kWinChat */
    WindowRef     win;
    TXNObject     transcript;
    TXNFrameID    transFrame;
    TXNObject     input;
    TXNFrameID    inputFrame;
    ControlHandle sendBtn;
    char          convId[128];
    Boolean       waiting;      /* a request is in flight */
    short          mood;
    short          animFrame;
    unsigned long  lastAnim;
    Rect           screenRect;   /* Max's CRT, for cheap animation redraws */
} ChatWin;

static ChatWin gChat;
static Boolean gChatInited = false;

static const RGBColor kBlack = { 0, 0, 0 };
static const RGBColor kWhite = { 0xFFFF, 0xFFFF, 0xFFFF };
static const RGBColor kBeige = { 0xDDDD, 0xD8D8, 0xCCCC };
static const RGBColor kBeigeD= { 0xAAAA, 0xA4A4, 0x9494 };
static const RGBColor kScreen= { 0x1010, 0x2828, 0x1818 };  /* dark CRT */
static const RGBColor kGray  = { 0x7777, 0x7777, 0x7777 };
static const RGBColor kGreen = { 0x3333, 0xE666, 0x5555 };  /* phosphor */

/* ---------------- geometry ---------------- */

static void ChatLayout(Rect* headRect, Rect* transRect, Rect* inputRect, Rect* btnRect)
{
    Rect port = gChat.win->portRect;
    short inputTop = port.bottom - kChatInputH;
    SetRect(headRect, port.left, port.top, port.right, port.top + kChatHeaderH);
    SetRect(transRect, port.left, port.top + kChatHeaderH, port.right, inputTop - 1);
    SetRect(btnRect, port.right - kChatGap - kChatBtnW,
            inputTop + (kChatInputH - kChatBtnH) / 2,
            port.right - kChatGap,
            inputTop + (kChatInputH - kChatBtnH) / 2 + kChatBtnH);
    SetRect(inputRect, port.left + kChatGap, inputTop + kChatGap,
            btnRect->left - kChatGap, port.bottom - kChatGap);
}

static void ChatApplyLayout(void)
{
    Rect hr, tr, ir, br;
    ChatLayout(&hr, &tr, &ir, &br);
    TXNSetFrameBounds(gChat.transcript, tr.top, tr.left, tr.bottom, tr.right, gChat.transFrame);
    TXNSetFrameBounds(gChat.input, ir.top, ir.left, ir.bottom, ir.right, gChat.inputFrame);
    if (gChat.sendBtn) {
        MoveControl(gChat.sendBtn, br.left, br.top);
        SizeControl(gChat.sendBtn, br.right - br.left, br.bottom - br.top);
    }
}

/* ---------------- Max, the little Mac Plus ---------------- */

static void DrawMaxFace(const Rect* screen, short mood, short frame)
{
    /* Draw a face on the CRT in phosphor green. */
    short cx = (screen->left + screen->right) / 2;
    short cy = (screen->top + screen->bottom) / 2;
    RGBForeColor(&kGreen);

    switch (mood) {
    case kMoodThinking: {
        /* three dots that light up in sequence */
        short i;
        for (i = 0; i < 3; i++) {
            Rect d;
            short x = cx - 6 + i * 6;
            SetRect(&d, x - 1, cy - 1, x + 1, cy + 1);
            if ((frame % 3) == i) PaintOval(&d);
            else FrameOval(&d);
        }
        break;
    }
    case kMoodHappy: {
        /* two eyes and a smile */
        Rect e;
        SetRect(&e, cx - 5, cy - 4, cx - 3, cy - 2); PaintOval(&e);
        SetRect(&e, cx + 3, cy - 4, cx + 5, cy - 2); PaintOval(&e);
        MoveTo(cx - 4, cy + 1); LineTo(cx - 2, cy + 3);
        LineTo(cx + 2, cy + 3); LineTo(cx + 4, cy + 1);
        break;
    }
    case kMoodOops: {
        Rect e;
        SetRect(&e, cx - 5, cy - 4, cx - 3, cy - 2); PaintOval(&e);
        SetRect(&e, cx + 3, cy - 4, cx + 5, cy - 2); PaintOval(&e);
        /* frown */
        MoveTo(cx - 4, cy + 3); LineTo(cx - 2, cy + 1);
        LineTo(cx + 2, cy + 1); LineTo(cx + 4, cy + 3);
        break;
    }
    default: {  /* idle: blinking eyes + gentle smile */
        Rect e;
        Boolean blink = (frame % 8) == 0;
        if (blink) {
            MoveTo(cx - 5, cy - 3); LineTo(cx - 3, cy - 3);
            MoveTo(cx + 3, cy - 3); LineTo(cx + 5, cy - 3);
        } else {
            SetRect(&e, cx - 5, cy - 4, cx - 3, cy - 2); PaintOval(&e);
            SetRect(&e, cx + 3, cy - 4, cx + 5, cy - 2); PaintOval(&e);
        }
        MoveTo(cx - 3, cy + 2); LineTo(cx + 3, cy + 2);
        break;
    }
    }
    RGBForeColor(&kBlack);
}

static void DrawMax(const Rect* head)
{
    /* A compact Mac (Mac Plus) about 34 x 38, on the left of the header. */
    Rect body, screen, slot, r;
    short x = head->left + 8;
    short y = head->top + 5;

    SetRect(&body, x, y, x + 34, y + 38);

    /* case */
    RGBForeColor(&kBeige);
    PaintRoundRect(&body, 6, 6);
    RGBForeColor(&kBeigeD);
    FrameRoundRect(&body, 6, 6);

    /* screen */
    SetRect(&screen, x + 5, y + 5, x + 29, y + 24);
    gChat.screenRect = screen;
    RGBForeColor(&kScreen);
    PaintRect(&screen);
    RGBForeColor(&kBeigeD);
    FrameRect(&screen);

    DrawMaxFace(&screen, gChat.mood, gChat.animFrame);

    /* floppy slot */
    SetRect(&slot, x + 9, y + 29, x + 25, y + 31);
    RGBForeColor(&kBeigeD);
    PaintRect(&slot);

    /* little vents */
    RGBForeColor(&kBeigeD);
    MoveTo(x + 9, y + 34);  LineTo(x + 24, y + 34);
    MoveTo(x + 9, y + 36);  LineTo(x + 24, y + 36);

    RGBForeColor(&kBlack);
    (void)r;
}

static const char* MoodLine(void)
{
    switch (gChat.mood) {
    case kMoodThinking: return "Thinking\311";
    case kMoodHappy:    return "Here you go.";
    case kMoodOops:     return "Hmm, that didn't work.";
    default:            return "Ready when you are.";
    }
}

static void DrawHeader(void)
{
    Rect hr, tr, ir, br;
    ChatLayout(&hr, &tr, &ir, &br);
    RGBForeColor(&kWhite); PaintRect(&hr); RGBForeColor(&kBlack);

    DrawMax(&hr);

    TextFont(applFont); TextSize(9);
    RGBForeColor(&kBlack);
    MoveTo(hr.left + 52, hr.top + 20);
    { Str255 p; CToPStr("Max \321 your writing helper", p); DrawString(p); }
    RGBForeColor(&kGray);
    MoveTo(hr.left + 52, hr.top + 34);
    { Str255 p; CToPStr(MoodLine(), p); DrawString(p); }
    RGBForeColor(&kGray);
    MoveTo(hr.left, hr.bottom - 1); LineTo(hr.right, hr.bottom - 1);
    RGBForeColor(&kBlack);
}

static void SetMood(short mood)
{
    gChat.mood = mood;
    gChat.animFrame = 0;
    gChat.lastAnim = TickCount();
    if (gChat.win) { SetPort(gChat.win); DrawHeader(); }
}

/* Redraw only Max's CRT — the single animating region — to avoid the
   whole-header flicker of a full repaint each frame. */
static void AnimateMax(void)
{
    if (gChat.win == NULL) return;
    if (EmptyRect(&gChat.screenRect)) { SetPort(gChat.win); DrawHeader(); return; }
    SetPort(gChat.win);
    RGBForeColor(&kScreen); PaintRect(&gChat.screenRect);
    RGBForeColor(&kBeigeD); FrameRect(&gChat.screenRect);
    DrawMaxFace(&gChat.screenRect, gChat.mood, gChat.animFrame);
    RGBForeColor(&kBlack);
}

/* ---------------- transcript helpers ---------------- */

static void TransAppend(const char* s, long n)
{
    TXNSetData(gChat.transcript, kTXNTextData, (void*)s, n, kTXNEndOffset, kTXNEndOffset);
}
static void TransAppendC(const char* s) { TransAppend(s, (long)strlen(s)); }

static void TransAppendStyled(const char* s, long n, Style face, const RGBColor* col)
{
    TXNOffset start, end;
    TransAppend(s, n);
    end = TXNDataSize(gChat.transcript);
    start = end - n;
    {
        TXNTypeAttributes a[2];
        ItemCount c = 0;
        a[c].tag = kTXNQDFontStyleAttribute; a[c].size = kTXNQDFontStyleAttributeSize;
        a[c].data.dataValue = face; c++;
        if (col) { a[c].tag = kTXNQDFontColorAttribute; a[c].size = kTXNQDFontColorAttributeSize;
                   a[c].data.dataPtr = (void*)col; c++; }
        TXNSetTypeAttributes(gChat.transcript, c, a, start, end);
    }
    TXNSetSelection(gChat.transcript, kTXNEndOffset, kTXNEndOffset);
    TXNShowSelection(gChat.transcript, false);
}

/* ---------------- JSON encoding ---------------- */

static void JAppend(Handle h, const void* d, long n)
{
    long cur = GetHandleSize(h);
    SetHandleSize(h, cur + n);
    if (MemError() == noErr) BlockMoveData(d, *h + cur, n);
}
static void JStr(Handle h, const char* s) { JAppend(h, s, (long)strlen(s)); }

static void JQuoted(Handle h, const char* s, long n)
{
    long i;
    JStr(h, "\"");
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  JStr(h, "\\\""); break;
        case '\\': JStr(h, "\\\\"); break;
        case '\r': JStr(h, "\\n");  break;
        case '\n': JStr(h, "\\n");  break;
        case '\t': JStr(h, "\\t");  break;
        default:
            if (c < 0x20) {
                char u[8]; const char* hex = "0123456789abcdef";
                u[0]='\\'; u[1]='u'; u[2]='0'; u[3]='0';
                u[4]=hex[(c>>4)&0xF]; u[5]=hex[c&0xF]; u[6]=0;
                JStr(h, u);
            } else { char one = (char)c; JAppend(h, &one, 1); }
        }
    }
    JStr(h, "\"");
}

/* ---------------- sending (starts the async request) ---------------- */

static void ChatSend(void)
{
    Handle msgH, docH = NULL, body;
    long msgLen, docLen = 0;
    ByteCount inLen;
    char modelC[64], keyHdr[300];
    OSStatus err;

    if (gChat.waiting) return;

    /* NB: TXNGetData needs real absolute offsets, not kTXNEndOffset */
    inLen = TXNDataSize(gChat.input);
    if (inLen == 0) return;
    if (TXNGetData(gChat.input, kTXNStartOffset, inLen, &msgH) != noErr || msgH == NULL)
        return;
    msgLen = GetHandleSize(msgH);
    {
        long i; Boolean any = false;
        HLock(msgH);
        for (i = 0; i < msgLen; i++) if ((*msgH)[i] > ' ') { any = true; break; }
        if (!any) { HUnlock(msgH); DisposeHandle(msgH); return; }
    }

    if (gPrefs.proxyHost[0] == 0) {
        HUnlock(msgH); DisposeHandle(msgH);
        ParamAlert(rErrorAlert,
            "\pSet the proxy address in Preferences before using the assistant.", "\p");
        return;
    }

    /* echo the user's line */
    if (TXNDataSize(gChat.transcript) > 0) TransAppendC("\r\r");
    { RGBColor blue = { 0x2222, 0x4444, 0xAAAA }; TransAppendStyled("You", 3, bold, &blue); }
    TransAppendC("\r");
    TransAppend(*msgH, msgLen);

    /* current note as context */
    docH = EditorGetText(&gMain);
    if (docH != NULL) { docLen = GetHandleSize(docH); if (docLen > kMaxDocChars) docLen = kMaxDocChars; }

    PToCStr(gPrefs.model, modelC, sizeof(modelC));

    body = NewHandle(0);
    JStr(body, "{\"model\":");
    JQuoted(body, modelC, (long)strlen(modelC));
    JStr(body, ",\"conversation\":");
    JQuoted(body, gChat.convId, (long)strlen(gChat.convId));
    JStr(body, ",\"message\":");
    JQuoted(body, *msgH, msgLen);
    HUnlock(msgH);
    JStr(body, ",\"document\":");
    if (docH != NULL) { HLock(docH); JQuoted(body, *docH, docLen); HUnlock(docH); }
    else JStr(body, "\"\"");
    JStr(body, "}");

    DisposeHandle(msgH);
    if (docH) DisposeHandle(docH);

    keyHdr[0] = 0;
    if (gPrefs.apiKey[0] > 0) {
        char keyC[256];
        PToCStr(gPrefs.apiKey, keyC, sizeof(keyC));
        strcpy(keyHdr, "X-Api-Key: ");
        strcat(keyHdr, keyC);
        strcat(keyHdr, "\r\n");
    }

    /* clear the input box */
    TXNSetData(gChat.input, kTXNTextData, (void*)"", 0, kTXNStartOffset, kTXNEndOffset);

    /* fire the request — returns immediately */
    HLock(body);
    err = NetStart(gPrefs.proxyHost, (short)gPrefs.proxyPort, "/chat",
                   keyHdr[0] ? keyHdr : NULL, *body, GetHandleSize(body));
    HUnlock(body);
    DisposeHandle(body);

    if (err != noErr) {
        NetReset();
        TransAppendC("\r\r");
        { RGBColor red = { 0xAAAA, 0x2222, 0x2222 }; TransAppendStyled("Max", 3, bold, &red); }
        TransAppendC("\r");
        if (err == paramErr)
            TransAppendC("(The proxy address must be a numeric IP, e.g. 192.168.1.10.)");
        else
            TransAppendC("(Could not start the request. Check Preferences and the proxy.)");
        SetMood(kMoodOops);
        TXNForceUpdate(gChat.transcript);
        return;
    }

    gChat.waiting = true;
    HiliteControl(gChat.sendBtn, 255);
    SetMood(kMoodThinking);
}

/* ---------------- completion (polled from the event loop) ---------------- */

static void ChatFinish(void)
{
    short state = NetState();
    OSStatus err = NetLastError();
    short http = NetHTTPStatus();
    Handle resp;

    gChat.waiting = false;
    if (gChat.sendBtn) HiliteControl(gChat.sendBtn, 0);

    /* remember the conversation id for the next turn */
    { const char* cid = NetConversationId();
      if (cid && cid[0]) { strncpy(gChat.convId, cid, sizeof(gChat.convId) - 1);
                           gChat.convId[sizeof(gChat.convId) - 1] = 0; } }

    TransAppendC("\r\r");
    { RGBColor green = { 0x2222, 0x6666, 0x3333 }; TransAppendStyled("Max", 3, bold, &green); }
    TransAppendC("\r");

    resp = NetResponseBody();

    if (state == kNetFailed || err != noErr) {
        if (err == kOTNoDataErr)
            TransAppendC("(The proxy didn't answer in time. Is it running?)");
        else
            TransAppendC("(Could not reach the proxy. Check Preferences and that it is running.)");
        SetMood(kMoodOops);
    } else if (http != 0 && (http < 200 || http >= 300)) {
        char m[48], num[8]; short d = 0, v = http, t[8], td = 0;
        while (v > 0) { t[td++] = '0' + v % 10; v /= 10; }
        while (td > 0) num[d++] = (char)t[--td]; num[d] = 0;
        strcpy(m, "(The proxy returned HTTP "); strcat(m, num); strcat(m, ".)");
        TransAppendC(m);
        if (resp && GetHandleSize(resp) > 0) {
            TransAppendC("\r");
            HLock(resp); TransAppend(*resp, GetHandleSize(resp)); HUnlock(resp);
        }
        SetMood(kMoodOops);
    } else if (resp && GetHandleSize(resp) > 0) {
        long rn = GetHandleSize(resp), i;
        HLock(resp);
        for (i = 0; i < rn; i++) if ((*resp)[i] == '\n') (*resp)[i] = '\r';
        TransAppend(*resp, rn);
        HUnlock(resp);
        SetMood(kMoodHappy);
    } else {
        TransAppendC("(No reply.)");
        SetMood(kMoodOops);
    }

    NetReset();

    TXNSetSelection(gChat.transcript, kTXNEndOffset, kTXNEndOffset);
    TXNShowSelection(gChat.transcript, false);
    TXNForceUpdate(gChat.transcript);
    TXNFocus(gChat.input, true);
}

void ChatPollNet(void)
{
    if (gChat.win == NULL) return;

    if (gChat.waiting) {
        short st = NetState();
        if (st == kNetDone || st == kNetFailed) {
            ChatFinish();
            return;
        }
        /* animate Max while he thinks (only if his window is up front) */
        if (FrontWindow() == gChat.win && TickCount() - gChat.lastAnim > 15) {
            gChat.animFrame++;
            gChat.lastAnim = TickCount();
            AnimateMax();
        }
    } else if (gChat.mood == kMoodIdle && FrontWindow() == gChat.win) {
        /* gentle idle blink */
        if (TickCount() - gChat.lastAnim > 40) {
            gChat.animFrame++;
            gChat.lastAnim = TickCount();
            AnimateMax();
        }
    }
}

/* ---------------- lifecycle ---------------- */

void ChatShow(MainWin* mw)
{
    Rect hr, tr, ir, br;
    (void)mw;

    if (!gPrefs.llmEnabled) {
        ParamAlert(rErrorAlert,
            "\pThe writing assistant is turned off. Enable it in Preferences.", "\p");
        return;
    }

    if (!gChatInited) { gChat.win = NULL; gChat.convId[0] = 0; gChatInited = true; }
    if (gChat.win != NULL) { SelectWindow(gChat.win); return; }

    {
        Rect bounds;
        short sh = qd.screenBits.bounds.bottom;
        SetRect(&bounds, 60, sh - 470, 60 + 400, sh - 40);
        if (bounds.top < 44) bounds.top = 44;
        gChat.kind = kWinChat;
        gChat.waiting = false;
        gChat.mood = kMoodIdle;
        gChat.animFrame = 0;
        gChat.lastAnim = TickCount();
        gChat.win = NewCWindow(NULL, &bounds, "\pWriting Assistant", true,
                               zoomDocProc, (WindowPtr)-1L, true, 0);
        if (gChat.win == NULL) return;
        SetWindowTag(gChat.win, &gChat);
        SetPort(gChat.win);
    }

    ChatLayout(&hr, &tr, &ir, &br);

    if (TXNNewObject(NULL, gChat.win, &tr,
                     kTXNWantVScrollBarMask | kTXNAlwaysWrapAtViewEdgeMask
                     | kTXNReadOnlyMask | kTXNNoKeyboardSyncMask | kTXNDisableDragAndDropMask,
                     kTXNTextEditStyleFrameType, kTXNTextFile,
                     kTXNSystemDefaultEncoding,
                     &gChat.transcript, &gChat.transFrame, 0) != noErr) {
        DisposeWindow(gChat.win); gChat.win = NULL; return;
    }
    if (TXNNewObject(NULL, gChat.win, &ir,
                     kTXNAlwaysWrapAtViewEdgeMask | kTXNNoKeyboardSyncMask | kTXNDisableDragAndDropMask,
                     kTXNTextEditStyleFrameType, kTXNTextFile,
                     kTXNSystemDefaultEncoding,
                     &gChat.input, &gChat.inputFrame, 0) != noErr) {
        TXNDeleteObject(gChat.transcript);
        DisposeWindow(gChat.win); gChat.win = NULL; return;
    }

    gChat.sendBtn = NewControl(gChat.win, &br, "\pSend", true, 0, 0, 0, pushButProc, 0);

    {
        RGBColor gray = { 0x7777, 0x7777, 0x7777 };
        const char* intro =
            "Hi, I'm Max. I can help you tighten prose, find a sharper word, or "
            "talk through structure. Ask away \321 the current note comes along "
            "as context.";
        TransAppendStyled(intro, (long)strlen(intro), italic, &gray);
    }

    ChatApplyLayout();
    TXNFocus(gChat.input, true);
}

void ChatHandleEvent(WindowRef w, EventRecord* ev)
{
    if (w != gChat.win) return;
    SetPort(w);
    ChatApplyLayout();

    switch (ev->what) {
    case updateEvt:
        BeginUpdate(w);
        EraseRect(&w->portRect);    /* clipped to the update region */
        DrawHeader();
        TXNDraw(gChat.transcript, NULL);
        TXNDraw(gChat.input, NULL);
        UpdateControls(w, w->visRgn);
        DrawGrowBox(w);
        EndUpdate(w);
        break;

    case activateEvt: {
        Boolean act = (ev->modifiers & activeFlag) != 0;
        TXNActivate(gChat.transcript, gChat.transFrame, kScrollBarsSyncWithFocus);
        TXNActivate(gChat.input, gChat.inputFrame, kScrollBarsSyncWithFocus);
        TXNFocus(gChat.input, act);
        if (gChat.sendBtn) HiliteControl(gChat.sendBtn, (act && !gChat.waiting) ? 0 : 255);
        DrawGrowBox(w);
        break;
    }

    case mouseDown: {
        Point pt = ev->where;
        ControlHandle c; short part;
        GlobalToLocal(&pt);
        part = FindControl(pt, w, &c);
        if (part && c == gChat.sendBtn) {
            if (TrackControl(c, pt, NULL) && !gChat.waiting) ChatSend();
        } else {
            Rect hr, tr, ir, br;
            ChatLayout(&hr, &tr, &ir, &br);
            if (PtInRect(pt, &ir)) TXNClick(gChat.input, ev);
            else if (PtInRect(pt, &tr)) TXNClick(gChat.transcript, ev);
        }
        break;
    }

    case keyDown:
    case autoKey: {
        char ch = ev->message & charCodeMask;
        if ((ch == '\r' || ch == 0x03) && !(ev->modifiers & shiftKey)) {
            if (!gChat.waiting) ChatSend();
        } else {
            TXNKeyDown(gChat.input, ev);
        }
        break;
    }
    }
}

/* Edit menu when the chat window is frontmost. Editing commands act on the
   input box; Copy prefers a selection in the transcript when there is one
   (that's what users mean when they copy from a chat). */
void ChatEditMenu(short item)
{
    if (gChat.win == NULL || gChat.input == NULL) return;

    if (item == iCopy && gChat.transcript != NULL) {
        TXNOffset s = 0, e = 0;
        TXNGetSelection(gChat.transcript, &s, &e);
        if (e > s) { TXNCopy(gChat.transcript); return; }
    }

    switch (item) {
    case iUndo:      TXNUndo(gChat.input); break;
    case iCut:       TXNCut(gChat.input); break;
    case iCopy:      TXNCopy(gChat.input); break;
    case iPaste:     TXNPaste(gChat.input); break;
    case iClear:     TXNClear(gChat.input); break;
    case iSelectAll: TXNSetSelection(gChat.input, kTXNStartOffset, kTXNEndOffset); break;
    }
}

void ChatClose(WindowRef w)
{
    if (w != gChat.win) return;
    if (gChat.waiting) { NetReset(); gChat.waiting = false; }
    if (gChat.sendBtn) { DisposeControl(gChat.sendBtn); gChat.sendBtn = NULL; }
    if (gChat.input)      { TXNDeleteObject(gChat.input); gChat.input = NULL; }
    if (gChat.transcript) { TXNDeleteObject(gChat.transcript); gChat.transcript = NULL; }
    SetWindowTag(gChat.win, NULL);
    DisposeWindow(gChat.win);
    gChat.win = NULL;
}

void ChatCloseIfOpen(void)
{
    if (gChat.win != NULL) ChatClose(gChat.win);
}

void ChatIdle(void)
{
    if (gChat.win != NULL && gChat.input != NULL)
        TXNIdle(gChat.input);
}
