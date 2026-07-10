/*
 * Hans — an asynchronous HTTP/1.0 client over Open Transport.
 *
 * Mac OS 9 cannot speak modern TLS, so Hans talks plain HTTP to the Hans
 * Proxy on the local network; the proxy terminates TLS and forwards to the
 * OpenAI API. The host is a dotted-quad IP given in Preferences (no DNS).
 *
 * The endpoint runs in synchronous *non-blocking* mode and is driven by a
 * small state machine that NetPump() advances a little on every pass of the
 * main event loop. Nothing blocks: the UI (and the assistant's animated
 * avatar) stays live while a request is in flight.
 */
#include "hans.h"
#include <OpenTransport.h>
#include <OpenTransportProviders.h>
#include <string.h>

#define kConnectTimeout  1800L   /* ticks (~30s) to establish a connection */
#define kOverallTimeout  7200L   /* ticks (~2 min) for the whole exchange   */

enum {
    NS_IDLE = 0,
    NS_CONNECTING,
    NS_SENDING,
    NS_RECEIVING,
    NS_PARSING,
    NS_DONE_OK,
    NS_DONE_ERR
};

static struct {
    short         phase;
    EndpointRef   ep;
    Handle        request;      /* full request bytes */
    long          reqLen, reqSent;
    Handle        response;     /* accumulates reply; trimmed to body at end */
    short         httpStatus;
    OSStatus      err;
    char          convId[128];
    unsigned long startTicks;
    unsigned long connectDeadline;
} gReq;

static Boolean gOTReady = false;

static OSStatus EnsureOT(void)
{
    if (!gOTReady) {
        OSStatus err = InitOpenTransport();
        if (err != noErr) return err;
        gOTReady = true;
    }
    return noErr;
}

static Boolean ParseDottedQuad(ConstStr255Param host, InetHost* out)
{
    long parts[4], p = 0, val = 0;
    short i, digits = 0, len = host[0];
    for (i = 1; i <= len; i++) {
        char c = host[i];
        if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0'); digits++;
            if (val > 255) return false;
        } else if (c == '.') {
            if (digits == 0 || p >= 3) return false;
            parts[p++] = val; val = 0; digits = 0;
        } else return false;
    }
    if (digits == 0 || p != 3) return false;
    parts[3] = val;
    *out = ((InetHost)parts[0] << 24) | ((InetHost)parts[1] << 16)
         | ((InetHost)parts[2] << 8) | (InetHost)parts[3];
    return true;
}

static Boolean AppendBytes(Handle h, const void* data, long len)
{
    long cur;
    if (h == NULL) return false;
    cur = GetHandleSize(h);
    SetHandleSize(h, cur + len);
    if (MemError() != noErr) return false;
    BlockMoveData(data, *h + cur, len);
    return true;
}

static void NetCleanupEndpoint(void)
{
    if (gReq.ep != kOTInvalidEndpointRef) {
        OTCloseProvider(gReq.ep);
        gReq.ep = kOTInvalidEndpointRef;
    }
    if (gReq.request) { DisposeHandle(gReq.request); gReq.request = NULL; }
}

/* Build the request bytes (headers + body) into gReq.request. */
static void BuildRequest(ConstStr255Param host, const char* path,
                         const char* extraHeaders,
                         const char* body, long bodyLen)
{
    char hostC[64];
    short n = host[0], i;
    Boolean ok = true;
    Handle r = NewHandle(0);

    gReq.request = NULL;
    if (r == NULL) return;

    if (n > 63) n = 63;
    for (i = 0; i < n; i++) hostC[i] = host[i + 1];
    hostC[n] = 0;

    ok = ok && AppendBytes(r, "POST ", 5);
    ok = ok && AppendBytes(r, path, (long)strlen(path));
    ok = ok && AppendBytes(r, " HTTP/1.0\r\n", 11);
    ok = ok && AppendBytes(r, "Host: ", 6);
    ok = ok && AppendBytes(r, hostC, (long)strlen(hostC));
    ok = ok && AppendBytes(r, "\r\n", 2);
    ok = ok && AppendBytes(r, "Content-Type: application/json\r\n", 32);
    if (extraHeaders && extraHeaders[0])
        ok = ok && AppendBytes(r, extraHeaders, (long)strlen(extraHeaders));
    {
        char num[16]; short d = 0; long v = bodyLen;
        if (v == 0) num[d++] = '0';
        else { char t[16]; short td = 0; while (v > 0) { t[td++] = '0' + (v % 10); v /= 10; }
               while (td > 0) num[d++] = t[--td]; }
        ok = ok && AppendBytes(r, "Content-Length: ", 16);
        ok = ok && AppendBytes(r, num, d);
        ok = ok && AppendBytes(r, "\r\n", 2);
    }
    ok = ok && AppendBytes(r, "Connection: close\r\n\r\n", 21);
    if (bodyLen > 0) ok = ok && AppendBytes(r, body, bodyLen);

    if (!ok) { DisposeHandle(r); return; }   /* caller sees request == NULL */

    gReq.request = r;
    gReq.reqLen = GetHandleSize(r);
    gReq.reqSent = 0;
}

OSStatus NetStart(ConstStr255Param host, short port, const char* path,
                  const char* extraHeaders, const char* body, long bodyLen)
{
    OSStatus err;
    InetHost hostAddr;
    InetAddress inAddr;
    TCall sndCall;

    if (gReq.phase != NS_IDLE) return -1;   /* one request at a time */

    gReq.ep = kOTInvalidEndpointRef;
    gReq.request = NULL;
    if (gReq.response) DisposeHandle(gReq.response);   /* stale from a failed start */
    gReq.response = NewHandle(0);
    gReq.httpStatus = 0;
    gReq.err = noErr;
    gReq.startTicks = TickCount();
    gReq.connectDeadline = gReq.startTicks + kConnectTimeout;

    if (gReq.response == NULL) { gReq.err = memFullErr; return memFullErr; }

    if (!ParseDottedQuad(host, &hostAddr)) { gReq.err = paramErr; return paramErr; }

    err = EnsureOT();
    if (err != noErr) { gReq.err = err; return err; }

    gReq.ep = OTOpenEndpoint(OTCreateConfiguration("tcp"), 0, NULL, &err);
    if (err != noErr || gReq.ep == kOTInvalidEndpointRef) {
        gReq.err = err ? err : -1;
        return gReq.err;
    }

    OTSetSynchronous(gReq.ep);
    OTSetNonBlocking(gReq.ep);

    err = OTBind(gReq.ep, NULL, NULL);
    if (err != noErr) { gReq.err = err; NetCleanupEndpoint(); return err; }

    BuildRequest(host, path, extraHeaders, body, bodyLen);
    if (gReq.request == NULL) {
        gReq.err = memFullErr; NetCleanupEndpoint(); return memFullErr;
    }

    OTInitInetAddress(&inAddr, (InetPort)port, hostAddr);
    OTMemzero(&sndCall, sizeof(sndCall));
    sndCall.addr.buf = (UInt8*)&inAddr;
    sndCall.addr.len = sizeof(inAddr);

    err = OTConnect(gReq.ep, &sndCall, NULL);
    if (err == noErr) {
        gReq.phase = NS_SENDING;         /* connected immediately */
    } else if (err == kOTNoDataErr) {
        gReq.phase = NS_CONNECTING;      /* completing asynchronously */
    } else {
        gReq.err = err; NetCleanupEndpoint(); return err;
    }
    return noErr;
}

/* Parse the accumulated response: status code, X-Conversation-Id, and trim
   the header block so gReq.response holds only the body. */
static void ParseResponse(void)
{
    long n = GetHandleSize(gReq.response);
    long i, headerEnd = -1;
    char* p;

    gReq.convId[0] = 0;
    HLock(gReq.response);
    p = *gReq.response;

    if (n > 12 && p[0]=='H' && p[1]=='T' && p[2]=='T' && p[3]=='P') {
        long sp = 0;
        while (sp < n && p[sp] != ' ') sp++;
        if (sp + 4 <= n)
            gReq.httpStatus = (short)((p[sp+1]-'0')*100 + (p[sp+2]-'0')*10 + (p[sp+3]-'0'));
    }

    for (i = 0; i + 3 < n; i++)
        if (p[i]=='\r'&&p[i+1]=='\n'&&p[i+2]=='\r'&&p[i+3]=='\n') { headerEnd = i + 4; break; }

    if (headerEnd > 0) {
        static const char* key = "x-conversation-id:";
        const long keyLen = 18;
        long j;
        for (j = 0; j + keyLen <= headerEnd; j++) {
            long k = 0;
            while (key[k]) {
                char c = p[j+k];
                if (c >= 'A' && c <= 'Z') c += 32;
                if (c != key[k]) break;
                k++;
            }
            if (key[k] == 0) {
                long v = j + k, o = 0;
                while (v < headerEnd && (p[v]==' '||p[v]=='\t')) v++;
                while (v < headerEnd && p[v] != '\r' && p[v] != '\n'
                       && o < (long)sizeof(gReq.convId) - 1)
                    gReq.convId[o++] = p[v++];
                gReq.convId[o] = 0;
                break;
            }
            while (j < headerEnd && p[j] != '\n') j++;
        }

        {
            long bodyN = n - headerEnd;
            BlockMoveData(p + headerEnd, p, bodyN);
            HUnlock(gReq.response);
            SetHandleSize(gReq.response, bodyN);
        }
    } else {
        HUnlock(gReq.response);
    }
}

void NetPump(void)
{
    if (gReq.phase == NS_IDLE || gReq.phase >= NS_DONE_OK) return;

    /* overall timeout guard */
    if (TickCount() - gReq.startTicks > kOverallTimeout) {
        gReq.err = kOTNoDataErr;
        NetCleanupEndpoint();
        gReq.phase = NS_DONE_ERR;
        return;
    }

    switch (gReq.phase) {

    case NS_CONNECTING: {
        OTResult look = OTLook(gReq.ep);
        if (look == T_CONNECT) {
            OSStatus e = OTRcvConnect(gReq.ep, NULL);
            if (e == noErr) gReq.phase = NS_SENDING;
            else if (e != kOTNoDataErr) { gReq.err = e; NetCleanupEndpoint(); gReq.phase = NS_DONE_ERR; }
        } else if (look == T_DISCONNECT) {
            OTRcvDisconnect(gReq.ep, NULL);
            gReq.err = -1; NetCleanupEndpoint(); gReq.phase = NS_DONE_ERR;
        } else if (TickCount() > gReq.connectDeadline) {
            gReq.err = kOTNoDataErr; NetCleanupEndpoint(); gReq.phase = NS_DONE_ERR;
        }
        break;
    }

    case NS_SENDING: {
        HLock(gReq.request);
        while (gReq.reqSent < gReq.reqLen) {
            OTResult r = OTSnd(gReq.ep, *gReq.request + gReq.reqSent,
                               gReq.reqLen - gReq.reqSent, 0);
            if (r >= 0) {
                gReq.reqSent += r;
            } else if (r == kOTFlowErr || r == kOTNoDataErr) {
                break;              /* buffer full — resume next pass */
            } else {
                gReq.err = r; HUnlock(gReq.request);
                NetCleanupEndpoint(); gReq.phase = NS_DONE_ERR; return;
            }
        }
        HUnlock(gReq.request);
        if (gReq.reqSent >= gReq.reqLen)
            gReq.phase = NS_RECEIVING;
        break;
    }

    case NS_RECEIVING: {
        char buf[1024];
        short guard = 0;
        for (;;) {
            OTFlags flags = 0;
            OTResult r = OTRcv(gReq.ep, buf, sizeof(buf), &flags);
            if (r > 0) {
                if (!AppendBytes(gReq.response, buf, r)) {
                    gReq.err = memFullErr;
                    NetCleanupEndpoint();
                    gReq.phase = NS_DONE_ERR;
                    return;
                }
                if (++guard > 16) break;     /* yield to the UI periodically */
            } else if (r == kOTNoDataErr) {
                break;                        /* nothing ready — try next pass */
            } else if (r == kOTLookErr) {
                OTResult look = OTLook(gReq.ep);
                if (look == T_ORDREL) { OTRcvOrderlyDisconnect(gReq.ep); gReq.phase = NS_PARSING; }
                else if (look == T_DISCONNECT) { OTRcvDisconnect(gReq.ep, NULL); gReq.phase = NS_PARSING; }
                else if (look == T_DATA) { continue; }
                else { gReq.phase = NS_PARSING; }
                break;
            } else {
                gReq.err = r; gReq.phase = NS_PARSING; break;
            }
        }
        if (gReq.phase == NS_PARSING) {
            ParseResponse();
            NetCleanupEndpoint();
            gReq.phase = (gReq.err == noErr) ? NS_DONE_OK : NS_DONE_ERR;
        }
        break;
    }
    }
}

short NetState(void)
{
    switch (gReq.phase) {
    case NS_IDLE:    return kNetIdle;
    case NS_DONE_OK: return kNetDone;
    case NS_DONE_ERR: return kNetFailed;
    default:         return kNetBusy;
    }
}

Handle NetResponseBody(void)   { return gReq.response; }
short  NetHTTPStatus(void)      { return gReq.httpStatus; }
const char* NetConversationId(void) { return gReq.convId; }
OSStatus NetLastError(void)     { return gReq.err; }

void NetReset(void)
{
    NetCleanupEndpoint();
    if (gReq.response) { DisposeHandle(gReq.response); gReq.response = NULL; }
    gReq.phase = NS_IDLE;
    gReq.httpStatus = 0;
    gReq.err = noErr;
}

/* Called once at quit: abort anything in flight and hand Open Transport
   back to the system (leaving a provider open across ExitToShell is a
   classic way to wedge Mac OS 9). */
void NetShutdown(void)
{
    NetReset();
    if (gOTReady) {
        CloseOpenTransport();
        gOTReady = false;
    }
}
