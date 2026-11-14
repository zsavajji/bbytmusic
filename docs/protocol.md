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
"album_id":"…","playlist_id":"…","params":"…","duration_s":214,"explicit":false,"plays":"2B plays",
"year":"1998"}`. For a song or a video `id` is the video id; for the others a browse id. `plays`
is YouTube's own text ("15M plays", "9.2M views") and `year` an album's or single's year; both
are "" when unknown. A **Track** is an Item in the queue:
the same fields, of which the player uses `id`, `title`, `artist`, `album`, `thumb`,
`duration_s`.

| `cmd` | Fields | `result` |
|---|---|---|
| `status` | | `{"version":"…","state":"stopped\|loading\|playing\|paused","index":0,"track":Track or null,"position_ms":0,"duration_ms":0,"queue_len":0,"error":"","repeat":"off\|all\|one"}` |
| `search` | `q`, `filter`: `all` (default), `songs`, `videos`, `albums`, `artists`, `playlists` | `{"sections":[{"title":"…","items":[Item]}],"continuation":"…"}` |
| `search_more` | `continuation` | as `search` |
| `browse` | `id`: the browse id of an album (`MPRE…`), a playlist (`VL…`), an artist (`UC…`) or a feed (`FEmusic_home`) | `{"kind":"album\|playlist","id":"…","title":"…","subtitle":"Album • 1998","second_subtitle":"11 songs • 40 minutes","artist":"…","artist_id":"…","year":"1998","thumb":"https://…","description":"…","playlist_id":"…","items":[Item]}`. An album's tracks carry the album's title, id and thumb, and the album's artist when the row names none. A playlist gives its first page only (about 100 tracks). An artist's id (a channel id, `UC…`) answers `{"kind":"artist","id":"…","title":"…","subtitle":"42.5K subscribers","thumb":"https://…","description":"…","items":[Item],"sections":[{"title":"Albums","items":[Item]}]}`: `items` are the top songs, `sections` the page's shelves in its own order and under its own titles (Albums, Singles & EPs, Videos, Featured on, Fans might also like), without the top songs. A feed (`FEmusic_home`, any `FE…`) answers `{"kind":"feed","id":"…","items":[],"sections":[…],"continuation":"…"}`: its shelves, and the token of the next ones ("" at the end). Every `browse` result carries `sections` and `continuation`, empty where they do not apply. |
| `browse_more` | `continuation` | `{"sections":[{"title":"…","items":[Item]}],"continuation":"…"}`: a feed's next shelves. |
| `history` | `limit` (default 50, at most 200) | `{"items":[Item]}`: the tracks listened to, the latest first, each once, each with `"played_at"` (unix seconds of the last time). They come from the store, so they are as a restored queue's tracks are: kind `song`, no `explicit`, `playlist_id`, `params`, `plays` or `year`. |
| `thumbs` | `urls`: [string], `size` (default 0: as the url says) | `{"files":{"<url>":"<path>"}}`: the urls already in the cache, each with its file's absolute path. The others are fetched in the background and each arrives as a `thumb` event. With a `size`, a `googleusercontent.com` url is asked for at `size` × `size` pixels as a JPEG. Files live in `<data_dir>/thumbs/`. |
| `suggest` | `q` | `{"suggestions":["…"]}` |
| `play` | `items`: [Item], `index` (default 0), `radio` (default false) | `{}`. Replaces the queue and starts. With `radio` true and exactly one song or video, the engine appends its radio and goes on appending as the queue runs low; the reply then comes once the radio's first page is in (about 0.6 s; the sound has started by then). If the radio cannot be fetched the song still plays and the reply is still ok. |
| `pause`, `resume`, `toggle`, `stop`, `next`, `prev` | | `{}`. `resume` or `toggle` after `stop` plays the current track again from its start. `prev` restarts the track when more than 3 s in, else goes to the one before. |
| `seek` | `ms` | `{}` |
| `jump` | `index` | `{}` |
| `queue` | | `{"items":[Track],"index":0}` |
| `queue_add` | `items`: [Item], `next` (default false: at the end) | `{}` |
| `queue_remove` | `index` | `{}` |
| `queue_move` | `from`, `to` | `{}`. `to` is the row's index after the move. |
| `queue_clear` | | `{}`. Removes every track but the current one, which plays on. |
| `queue_shuffle` | | `{}`. Shuffles the tracks after the current one. |
| `queue_sort` | `by`: `title`, `artist`, `album` or `duration`; `desc` (default false) | `{}`. Sorts the whole queue (text without regard to case, equal rows keep their order); the current track stays the current one. |
| `repeat` | `mode`: `off`, `all` or `one` | `{}`. `all` goes back to the first track after the last; `one` plays the current track again when it ends (Next still moves on). Kept across restarts. |
| `quit` | | `{}`, then the engine exits |

Only songs and videos can be played or queued; an Item of another kind gets `code`
`bad_request`, as does a missing or wrongly typed field. An index outside the queue gets
`not_found`. `network` means YouTube could not be reached or answered with an error.

In `status`, `index` is -1 when the queue is empty; `stopped` still carries the current track.
After a restart the queue comes back paused, and its tracks have lost `explicit`,
`playlist_id` and `params` (the store keeps no columns for them).

Commands answer without waiting for the sound: `seek`, `next` and the rest are acknowledged
before anything changes, and what happens next comes as events. The exceptions are the ones
that ask YouTube (`search`, `search_more`, `suggest`, and `play` with `radio`).

A search for `all` that YouTube answers as one flat list comes back grouped by kind, in the
sections "Top result", "Songs", "Albums", "Artists", "Videos" and "Playlists" (singles and EPs
are albums; the empty ones are left out).

Every change to the queue (`play`, `queue_add`, `queue_remove`, `queue_move`, `queue_clear`,
`queue_shuffle`, `queue_sort`, a radio page arriving) is followed by a `queue` event, and every `track` event by a `position` event.

## Events

| `ev` | Fields | When |
|---|---|---|
| `state` | `state`, `index`, `track_id`, `position_ms`, `duration_ms`, `error` | on every change of state |
| `track` | `index`, `track`: Track | the current track changed, also at a gapless boundary |
| `position` | `position_ms`, `duration_ms` | about once a second while playing |
| `queue` | `len`, `index` | the queue's content changed; ask `queue` for it |
| `error` | `message`, `track_id` | a track could not be played; the player has moved on |
| `mode` | `repeat` | the repeat mode changed |
| `thumb` | `url`, `size`, `file` | a thumbnail asked for with `thumbs` is in the cache; `url` and `size` are the request's |

A UI that connects late sends `status` and `queue` and is then up to date.
