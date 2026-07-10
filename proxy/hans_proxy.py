#!/usr/bin/env python3
"""
Hans Proxy — the bridge between the classic Mac and the OpenAI API.

Mac OS 9 cannot speak modern TLS, so the Hans editor talks plain HTTP to
this daemon over your LAN. The proxy terminates TLS, calls the OpenAI
Conversations + Responses API (keeping each thread's state server-side via
a conversation id), and hands the assistant's reply back as plain text.

Standard library only — no pip installs.

    export OPENAI_API_KEY=sk-...
    python3 hans_proxy.py --host 0.0.0.0 --port 8079

Then in Hans → Preferences, set the proxy address to this machine's LAN IP
(e.g. 192.168.1.10) and the port to 8079.

Protocol (what the Mac sends):
    POST /chat  Content-Type: application/json
    { "model": "...", "conversation": "<id or empty>",
      "message": "...", "document": "..." }
    optional header  X-Api-Key: sk-...   (overrides OPENAI_API_KEY)

Response:
    200 OK, body = assistant reply as text/plain
    header  X-Conversation-Id: <id>   (send it back next turn to continue)
"""

import argparse
import json
import os
import sys
import urllib.request
import urllib.error
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

OPENAI_BASE = "https://api.openai.com/v1"

SYSTEM_PROMPT = (
    "You are Hans, a concise writing assistant living inside a Mac OS 9 "
    "Markdown editor. Give practical, specific advice on the user's prose: "
    "tighten wording, suggest sharper phrasing, point out cliches, filler, "
    "and hedging, and help with structure and flow. Prefer short replies in "
    "plain text (the editor shows plain text, so avoid Markdown headings, "
    "tables, or long bullet lists). When the user refers to 'the document' "
    "or 'this note', they mean the note included below as context."
)

DEFAULT_MODEL = "gpt-5-mini"


def _api_call(path, payload, api_key):
    """POST JSON to the OpenAI API and return the parsed JSON response."""
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        OPENAI_BASE + path,
        data=data,
        method="POST",
        headers={
            "Authorization": "Bearer " + api_key,
            "Content-Type": "application/json",
        },
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        return json.loads(resp.read().decode("utf-8"))


def create_conversation(api_key):
    """Create a new server-side conversation and return its id."""
    result = _api_call("/conversations", {}, api_key)
    return result.get("id", "")


def extract_text(response):
    """Pull the assistant's text out of a Responses API result."""
    # Convenience field first (present in most SDK-shaped responses).
    text = response.get("output_text")
    if isinstance(text, str) and text.strip():
        return text
    chunks = []
    for item in response.get("output", []):
        if item.get("type") == "message":
            for part in item.get("content", []):
                if part.get("type") in ("output_text", "text"):
                    chunks.append(part.get("text", ""))
    if chunks:
        return "".join(chunks)
    # Surface refusals or tool notes if that's all we got.
    for item in response.get("output", []):
        for part in item.get("content", []):
            if "refusal" in part:
                return part["refusal"]
    return "(The model returned no text.)"


def build_instructions(document):
    document = (document or "").strip()
    if not document:
        return SYSTEM_PROMPT + "\n\n(The current note is empty.)"
    # keep the context bounded
    if len(document) > 16000:
        document = document[:16000] + "\n...(truncated)"
    return SYSTEM_PROMPT + "\n\nCurrent note:\n\"\"\"\n" + document + "\n\"\"\""


MOCK = False


def handle_chat(body, api_key):
    """Run one turn. Returns (reply_text, conversation_id)."""
    model = body.get("model") or DEFAULT_MODEL
    message = body.get("message", "")
    document = body.get("document", "")
    conversation = (body.get("conversation") or "").strip()

    if MOCK:
        doc = (document or "").strip()
        words = len(doc.split())
        reply = (
            "Mock assistant (no API key needed).\n"
            "You said: " + (message or "(nothing)") + "\n"
            "Your note is about %d words. A quick tip: read it aloud and cut "
            "the first adjective in every sentence that still works without it."
            % words
        )
        return reply, conversation or "mock-conversation"

    if not conversation:
        conversation = create_conversation(api_key)

    payload = {
        "model": model,
        "instructions": build_instructions(document),
        "input": [{"role": "user", "content": message}],
    }
    if conversation:
        payload["conversation"] = conversation

    response = _api_call("/responses", payload, api_key)
    reply = extract_text(response)
    # The response echoes the conversation id if one was used.
    conversation = response.get("conversation") or conversation
    if isinstance(conversation, dict):
        conversation = conversation.get("id", "")
    return reply, conversation


class Handler(BaseHTTPRequestHandler):
    server_version = "HansProxy/1.0"
    protocol_version = "HTTP/1.0"  # match the classic Mac client

    def log_message(self, fmt, *args):
        sys.stderr.write("[hans-proxy] " + (fmt % args) + "\n")

    def _send(self, status, text, conv_id=None, ctype="text/plain"):
        # The client is a classic Mac: it renders MacRoman, not UTF-8.
        # Curly quotes, em dashes, accents etc. all exist in MacRoman;
        # anything that doesn't (emoji...) degrades to "?".
        body = text.encode("mac_roman", "replace")
        self.send_response(status)
        self.send_header("Content-Type", ctype + "; charset=macintosh")
        self.send_header("Content-Length", str(len(body)))
        if conv_id:
            self.send_header("X-Conversation-Id", conv_id)
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path in ("/health", "/"):
            self._send(200, "Hans Proxy is running.\n")
        else:
            self._send(404, "Not found.\n")

    def do_POST(self):
        if self.path != "/chat":
            self._send(404, "Not found.\n")
            return

        length = int(self.headers.get("Content-Length", "0") or "0")
        raw = self.rfile.read(length) if length else b""
        try:
            # The Mac writes MacRoman bytes into the JSON (curly quotes,
            # accents typed with option keys...). Try UTF-8 first for
            # modern test clients, then fall back to MacRoman, which
            # accepts any byte.
            try:
                decoded = raw.decode("utf-8")
            except UnicodeDecodeError:
                decoded = raw.decode("mac_roman")
            body = json.loads(decoded) if raw else {}
        except ValueError:
            self._send(400, "Malformed request body.\n")
            return

        api_key = self.headers.get("X-Api-Key") or os.environ.get("OPENAI_API_KEY", "")
        if not api_key and not MOCK:
            self._send(401, "No API key. Set OPENAI_API_KEY on the proxy "
                            "or enter one in Hans Preferences.\n")
            return

        try:
            reply, conv_id = handle_chat(body, api_key)
            self._send(200, reply, conv_id)
        except urllib.error.HTTPError as e:
            detail = e.read().decode("utf-8", "replace")
            self._send(502, "OpenAI API error (HTTP %d):\n%s\n" % (e.code, detail))
        except urllib.error.URLError as e:
            self._send(504, "Could not reach OpenAI: %s\n" % e.reason)
        except Exception as e:  # noqa: BLE001 - report anything back to the Mac
            self._send(500, "Proxy error: %s\n" % e)


def main():
    ap = argparse.ArgumentParser(description="Hans Proxy for classic Mac OS.")
    ap.add_argument("--host", default="0.0.0.0",
                    help="address to bind (default: all interfaces)")
    ap.add_argument("--port", type=int, default=8079, help="port (default 8079)")
    ap.add_argument("--model", default=None,
                    help="override the default model when the client sends none")
    ap.add_argument("--mock", action="store_true",
                    help="reply with canned advice; no API key or network needed")
    args = ap.parse_args()

    if args.model:
        global DEFAULT_MODEL
        DEFAULT_MODEL = args.model
    if args.mock:
        global MOCK
        MOCK = True
        sys.stderr.write("[hans-proxy] MOCK mode: canned replies, no OpenAI calls.\n")

    if not os.environ.get("OPENAI_API_KEY"):
        sys.stderr.write("[hans-proxy] Note: OPENAI_API_KEY is not set; the Mac "
                         "must send an X-Api-Key header instead.\n")

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    sys.stderr.write("[hans-proxy] listening on %s:%d\n" % (args.host, args.port))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        sys.stderr.write("\n[hans-proxy] shutting down.\n")
        server.shutdown()


if __name__ == "__main__":
    main()
