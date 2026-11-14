# BBMusic: the UI and engine protocol

What `bbmusicd` (the engine) and the UI say to each other. It follows HyperBerry's
`docs/protocol.md`. Written from the code of `engine/src/server/`; when the code changes, this
file changes with it.

## Connecting

1. The engine listens on `127.0.0.1`, on a port the system picks. At each start it writes
   `<data_dir>/engine.json`, mode 0600 (a temporary file renamed into place):
   `{"port":41234,"token":"<32 hex characters>"}`. The token is new at every start.
2. The UI reads the file and connects over TCP.
3. The first line must be `hello` with the token and `"proto":1`. Another command first, or a
   wrong token, gets an `auth` error and the connection closes; a wrong `proto` gets `proto`
   and closes. The reply to `hello` is `{"@":"1","ok":true,"result":{"proto":1}}`.
4. A missing file or a refused connection means the engine is not running. The engine removes
   `engine.json` when it stops cleanly.

## Framing

- One JSON object per line, UTF-8, ending in `\n`. A line is at most 1 MiB.
- **Requests** carry `"@"` (an id, a string the UI chooses) and `"cmd"`, and their own fields.
- **Replies** echo the id and come in request order on a connection.
  - success: `{"@":"7","ok":true}`, with `"result":{...}` when there is one
  - failure: `{"@":"7","ok":false,"error":{"code":"...","message":"..."}}`. Switch on `code`;
    `message` is English text for logs.
- **Events** carry `"ev"` and no `"@"`: `{"ev":"state","playing":true}`. Every authenticated
  connection gets every event.
- Errors the core gives: `auth`, `proto` (both close the connection), `bad_request` (invalid
  JSON, no `cmd`, or a line over 1 MiB; the connection stays open), `unknown` (no such
  command), `internal` (the handler threw). A line that grows past 4 MiB without a newline
  closes the connection.
- A client that stops reading falls behind; once 256 lines are queued for it, the engine
  closes that connection. The UI reconnects and asks for the current state.
- Ids are strings everywhere. Never parse them as numbers.

## Commands

Filled in as the commands land.

## Events

Filled in as the events land.
