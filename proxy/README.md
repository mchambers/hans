# Hans Proxy

The bridge between the classic Mac and the OpenAI API. Mac OS 9 can't speak
modern TLS, so Hans talks **plain HTTP** to this daemon over your LAN; the
proxy terminates TLS and calls the OpenAI **Conversations** + **Responses**
API, keeping each thread's state server-side via a conversation id.

Standard library only — nothing to `pip install`.

## Run

```sh
export OPENAI_API_KEY=sk-...
python3 hans_proxy.py --host 0.0.0.0 --port 8079
```

Options:

- `--host`  address to bind (default `0.0.0.0`, all interfaces)
- `--port`  port (default `8079`)
- `--model` default model if the client sends none (default `gpt-5-mini`)
- `--mock`  reply with canned advice; no API key or network needed

## Wire protocol

The Mac sends:

```
POST /chat  HTTP/1.0
Content-Type: application/json
X-Api-Key: sk-...        (optional; overrides OPENAI_API_KEY)

{ "model": "gpt-5-mini",
  "conversation": "",     // empty on the first turn, then echoed back
  "message": "Is my intro too wordy?",
  "document": "…the full current note…" }
```

The proxy replies:

```
HTTP/1.0 200 OK
Content-Type: text/plain; charset=macintosh
X-Conversation-Id: conv_abc123     // send this back next turn

Your intro leans on "at the end of the day" and two "very"s…
```

Replies are encoded as **MacRoman** (what the classic Mac renders); incoming
request bodies are decoded as UTF-8 with a MacRoman fallback, so text typed
with option-key accents on the Mac survives the round trip.

- `GET /health` returns `200` with a short status line.
- Errors come back with a non-2xx status and a plain-text explanation in
  the body, which Hans shows in the transcript.

## How a turn works

1. If the incoming `conversation` is empty, the proxy creates one
   (`POST /v1/conversations`) and returns its id in `X-Conversation-Id`.
2. It calls `POST /v1/responses` with the chosen model, the writing-
   assistant system prompt plus the current note as `instructions`, the
   user's message as `input`, and the `conversation` id so prior turns are
   remembered server-side.
3. It extracts the assistant's text and returns it as the response body.

The note is sent fresh every turn (so edits are always reflected) and is
bounded to keep requests small.

## Security

- Runs HTTP in the clear on your LAN — intended for a trusted home network.
  Don't expose it to the public internet.
- The API key lives on the proxy machine (env var) or is sent per-request
  by the Mac; it never needs to be stored on disk on the Mac beyond its
  preferences file.
