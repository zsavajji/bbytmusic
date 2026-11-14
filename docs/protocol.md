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

Every reply is `{"@":id,"ok":true,"result":{…}}` or an error. An **Item** is what a search or a
queue returns for one thing: `{"kind":"song|video|album|artist|playlist|station","id":"…",
"title":"…","subtitle":"…","thumb":"https://…","artist":"…","album":"…","artist_id":"…",
"album_id":"…","playlist_id":"…","params":"…","duration_s":214,"explicit":false}`. For a song or
a video `id` is the video id; for the others a browse id. A **Track** is an Item in the queue:
the same fields, of which the player uses `id`, `title`, `artist`, `album`, `thumb`,
`duration_s`.

| `cmd` | Fields | `result` |
|---|---|---|
| `status` | | `{"version":"…","state":"stopped\|loading\|playing\|paused","index":0,"track":Track or null,"position_ms":0,"duration_ms":0,"queue_len":0,"error":""}` |
| `search` | `q`, `filter`: `all` (default), `songs`, `videos`, `albums`, `artists`, `playlists` | `{"sections":[{"title":"…","items":[Item]}],"continuation":"…"}` |
| `search_more` | `continuation` | as `search` |
| `suggest` | `q` | `{"suggestions":["…"]}` |
| `play` | `items`: [Item], `index` (default 0), `radio` (default false) | `{}`. Replaces the queue and starts. With `radio` true and one song, the engine appends that song's radio, and goes on appending as the queue runs low. |
| `pause`, `resume`, `toggle`, `stop`, `next`, `prev` | | `{}` |
| `seek` | `ms` | `{}` |
| `jump` | `index` | `{}` |
| `queue` | | `{"items":[Track],"index":0}` |
| `queue_add` | `items`: [Item], `next` (default false: at the end) | `{}` |
| `queue_remove` | `index` | `{}` |
| `queue_move` | `from`, `to` | `{}` |
| `quit` | | `{}`, then the engine exits |

Only songs and videos can be played or queued; an Item of another kind gets `code`
`bad_request`. Error codes beyond the framing's: `network` (YouTube could not be reached or
answered with an error), `not_found`, `bad_request`.

Commands answer at once: `play`, `seek` and the rest are acknowledged before the sound changes,
and what happens next comes as events.

## Events

| `ev` | Fields | When |
|---|---|---|
| `state` | `state`, `index`, `track_id`, `position_ms`, `duration_ms`, `error` | on every change of state |
| `track` | `index`, `track`: Track | the current track changed, also at a gapless boundary |
| `position` | `position_ms`, `duration_ms` | about once a second while playing |
| `queue` | `len`, `index` | the queue's content changed; ask `queue` for it |
| `error` | `message`, `track_id` | a track could not be played; the player has moved on |

A UI that connects late sends `status` and `queue` and is then up to date.
