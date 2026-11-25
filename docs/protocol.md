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
queue returns for one thing: `{"kind":"song|video|episode|show|album|artist|playlist|station","id":"…",
"title":"…","subtitle":"…","thumb":"https://…","artist":"…","album":"…","artist_id":"…",
"album_id":"…","playlist_id":"…","params":"…","duration_s":214,"explicit":false,"plays":"2B plays",
"year":"1998"}`. For a song, a video or an episode `id` is the video id; for the others a browse id. `plays`
is YouTube's own text ("15M plays", "9.2M views") and `year` an album's or single's year; both
are "" when unknown. An **episode** is a podcast episode (it streams like a video): `artist` is its
show's name, `album_id` its show's browse id (`MPSP…`, which `browse` answers with a page of kind
`show`; `artist_id` stays "" and `album` too), `subtitle` YouTube's own "148K views • Apr 16", `plays` the views, and
`thumb` is a 16:9 video frame; `duration_s` is 0 for a live stream. Episodes come in a feed's shelves
(Home's Podcasts chip, see `browse`), in a show's page and in an `all` search. A **show** (a search result) is a
podcast: `id` its browse id (`MPSP…`, which `browse` answers with a page of kind `show`), `title` its name, `artist` its
publisher (with `artist_id` its channel's, when YouTube links it), `thumb` a square cover; it cannot be played. A **Track** is an Item in the queue:
the same fields, of which the player uses `id`, `title`, `artist`, `album`, `thumb`,
`duration_s`.

| `cmd` | Fields | `result` |
|---|---|---|
| `status` | | `{"version":"…","state":"stopped\|loading\|playing\|paused","index":0,"track":Track or null,"position_ms":0,"duration_ms":0,"queue_len":0,"error":"","repeat":"off\|all\|one","shuffle":false,"eq":"off"}` |
| `search` | `q`, `filter`: `all` (default), `songs`, `videos`, `albums`, `artists`, `playlists`, `podcasts`; any other name is `bad_request`. Each name but `all` is a YouTube search chip, sent as `params` (`playlists` is the "Community playlists" chip; `podcasts` is the "Podcasts" chip, whose shelf "Podcasts" holds shows of kind `show`; there is no filter for episodes) | `{"sections":[{"title":"…","items":[Item]}],"continuation":"…"}` |
| `search_more` | `continuation` | as `search` |
| `browse` | `id`: the browse id of an album (`MPRE…`), a playlist (`VL…`), an artist (`UC…`), a podcast show (`MPSP…`) or a feed (`FEmusic_home`); `params` (optional): a string passed to YouTube with the id | `{"kind":"album\|playlist\|show","id":"…","title":"…","subtitle":"Album • 1998","second_subtitle":"11 songs • 40 minutes","artist":"…","artist_id":"…","year":"1998","thumb":"https://…","description":"…","playlist_id":"…","items":[Item]}`. An album's tracks carry the album's title, id and thumb, and the album's artist when the row names none. A playlist gives its first page (about 100 tracks) and, when there are more, a `continuation` for `browse_more`. An artist's id (a channel id, `UC…`) answers `{"kind":"artist","id":"…","title":"…","subtitle":"42.5K subscribers","thumb":"https://…","description":"…","items":[Item],"sections":[{"title":"Albums","items":[Item]}]}`: `items` are the top songs, `sections` the page's shelves in its own order and under its own titles (Albums, Singles & EPs, Videos, Featured on, Fans might also like), without the top songs. A feed (`FEmusic_home`, any `FE…`) answers `{"kind":"feed","id":"…","items":[],"sections":[…],"continuation":"…"}`: its shelves, and the token of the next ones ("" at the end). A show (`MPSP…`, the `album_id` of its episodes) answers `{"kind":"show","id":"…","title":"…","artist":"…","artist_id":"UC…","thumb":"https://…","description":"…","items":[Item],"continuation":"…"}`: `title` is the show's name, `artist` its channel (`artist_id` the channel's page), `items` its episodes, the latest first, 20 of them and a `continuation` for `browse_more`; the page puts the show's name and `album_id` on every episode that carries none. `params` is only for a feed: Home's chip "Podcasts" is `FEmusic_home` with `params` `ggNCSgQIDBADSgQICRABSgQIBxABSgQICBABSgQIBBABSgQIDhABSgQIAxABSgQIDRABSgQIChABSgQIBhABSgQIBRAB` (taken from Home's own chips, 2026-11-15; there is no browse id of its own), answered as any feed: `sections` of episodes, a genre each (Fiction, Religion, Films, …), and a `continuation` for more genres. Every `browse` result carries `sections` and `continuation`, empty where they do not apply. The account's library needs a signed-in account (`signed_out` otherwise): `FEmusic_last_played` (Recent activity), `FEmusic_liked_playlists` (Playlists), `FEmusic_liked_albums` (Albums), `FEmusic_liked_videos` (Liked songs) and `FEmusic_library_corpus_artists` (Subscriptions) each answer as a feed with one section titled as the tab (no section when the tab is empty), and `VLLM` is Liked Music, a playlist. A playlist read through the account answers as a playlist page with about 15 tracks per page and a `continuation` (`tvp:…`), whose items carry `artist` but no album. A playlist the anonymous client cannot read (a private one) is asked again with the account when there is one. |
| `browse_more` | `continuation` | `{"items":[Item],"sections":[{"title":"…","items":[Item]}],"continuation":"…"}`: a feed's next shelves (in `sections`), a playlist's next tracks or a show's next episodes (in `items`; these carry no show name or `album_id`, the page's is theirs); the other is empty. `continuation` is "" at the end. A continuation beginning `tvf:` or `tvp:` is the account's: a library tab's next items come as one section with an empty title (`tvf:`), a playlist's next tracks in `items` (`tvp:`). |
| `info` | `id`: a video id | Item: the track as YouTube Music's own queue describes it (its radio's first entry), with `artist_id`, `album` and `album_id` where it has them. For a track that came without them: the account's lists carry none. `not_found` when YouTube's answer does not hold the track. |
| `like` | `id`: a video id; `on` (optional): true to like, false to remove the like | `{"id":"…","on":true}`: whether the signed-in account likes the video on YouTube. With `on` it is set first; without, YouTube is asked (the TV client's `next`, about 160 kB). `signed_out` without an account. |
| `history` | `limit` (default 50, at most 200) | `{"items":[Item]}`: the tracks listened to, the latest first, each once, each with `"played_at"` (unix seconds of the last time). They come from the store, so they are as a restored queue's tracks are: kind `song`, no `explicit`, `playlist_id`, `params`, `plays` or `year`. |
| `favorites` | | `{"items":[Item]}`: the favourites, the newest first, each with `"added_at"` (unix seconds) and `"offline"` (bool). `internal` when the store is not available. |
| `favorite` | `id`; or `item`: Item and `on`: bool | `{"id":"…","on":true}`. With only `id` it reports whether that track is a favourite. With `on` true and an `item` (a song, video or episode with an id; `bad_request` otherwise) it adds it; with `on` false and an `id` (or an `item`) it removes it. Adding one that is already there, or removing one that is not, succeeds. A `favorite` event follows every change; adding also starts the download of its audio. `internal` when the store is not available. |
| `thumbs` | `urls`: [string], `size` (default 0: as the url says) | `{"files":{"<url>":"<path>"}}`: the urls already in the cache, each with its file's absolute path. The others are fetched in the background and each arrives as a `thumb` event. With a `size`, a `googleusercontent.com` url is asked for at `size` × `size` pixels as a JPEG. Files live in `<data_dir>/thumbs/`. |
| `suggest` | `q` | `{"suggestions":["…"]}` |
| `play` | `items`: [Item], `index` (default 0), `radio` (default false) | `{}`. Replaces the queue and starts. With `radio` true and exactly one song or video (an episode has no radio: `radio` is ignored for it), the engine appends its radio and goes on appending as the queue runs low; the reply then comes once the radio's first page is in (about 0.6 s; the sound has started by then). If the radio cannot be fetched the song still plays and the reply is still ok. |
| `radio` | | `{}`. The current track plays on and the rest of the queue is replaced by its radio, which goes on being appended as the queue runs low, as with `play`; the reply comes once the radio's first page is in. If the radio cannot be fetched the track still plays on, alone, and the reply is still ok. `not_found` when the queue is empty. |
| `pause`, `resume`, `toggle`, `stop`, `next`, `prev` | | `{}`. `resume` or `toggle` after `stop` plays the current track again from its start. `prev` restarts the track when more than 3 s in, else goes to the one before. |
| `seek` | `ms` | `{}` |
| `jump` | `index` | `{}` |
| `queue` | | `{"items":[Track],"index":0}` |
| `queue_add` | `items`: [Item], `next` (default false: at the end) | `{}` |
| `queue_remove` | `index` | `{}` |
| `queue_move` | `from`, `to` | `{}`. `to` is the row's index after the move. |
| `queue_clear` | | `{}`. Removes every track but the current one, which plays on. |
| `queue_shuffle` | | `{}`. Shuffles the tracks after the current one, once; the shuffle mode is not changed. |
| `shuffle` | `on`: bool | `{}`. The shuffle mode (the user, 2026-11-14: "shuffle should be a toggle"). On: the tracks after the current one are shuffled and the order they had is remembered. Off: the queue goes back to that order, the current track staying the current one; tracks added meanwhile are where `queue_add` would have put them. While on, a `play` starts with the chosen track and shuffles the rest; `queue_sort` makes its order the remembered one and turns the mode off. The mode is kept across restarts, the remembered order is not (off then leaves the queue as it is). |
| `queue_sort` | `by`: `title`, `artist`, `album` or `duration`; `desc` (default false) | `{}`. Sorts only the tracks after the current one (text without regard to case, equal rows keep their order); the played ones and the current one stay put. |
| `eq` | `preset` (optional) | `{"preset":"off","presets":["off","bass_boost",…]}`: the equalizer's preset in use and the ones there are, `off` first. With `preset` it is set first (`bad_request` if there is no such preset), takes effect within a moment on what is playing, and is kept across restarts. |
| `skip` | `categories` (optional): [string] | `{"categories":["music_offtopic"],"all":["sponsor","intro","outro","music_offtopic"]}`: the kinds of segment the engine skips (SponsorBlock's names), and the ones it knows. With `categories` they are set first (`bad_request` for one it does not know) and kept across restarts. Empty, the default, means off: SponsorBlock is then never asked. When on, each track's segments are asked for by the first four hex digits of its id's SHA-256, never by the id. |
| `region` | `gl` (optional): two uppercase letters, like `IT` | `{"gl":"IT"}`: the country the content is asked for, in use now. With `gl` it is set first (`bad_request` unless two uppercase letters), kept across restarts, and a `region` event follows; the Home feed, `search` and the rest are asked for with it from then on (a feed's older continuations are for the old one). YouTube applies it to some shelves (Home's Quick picks, trending songs) and not to others (featured playlists and new releases follow the engine's IP; checked 2026-11-15). The language stays English: the item parser reads English labels. A user who never chose one gets the country YouTube sees the engine in (music.youtube.com/sw.js_data, no other service), asked for once, by the first command that asks YouTube after the first start; if that fails it is `US` for that run and asked again at the next start. The bare form only reports; it does not ask. |
| `cache` | `cap_mb` (optional): 128, 256, 512, 1024 or 2048; `clear` (optional): true | `{"bytes":0,"cap_mb":256}`: the songs kept on disk for replay without the network. `cap_mb` sets the most kept (`bad_request` for any other number), kept across restarts, the least recently played evicted first, default 256. `clear` deletes all but the track open now and the favourites. Favourites are never evicted and survive `clear`; they count toward `cap_mb`, and one that does not fit stays not offline until the cap is raised. The bare form only reports. |
| `account` | `import` (optional): `{"refresh_token":"…","client_id":"…","client_secret":"…"}`, as the Tube app's card gives it (`client_id` and `client_secret` both or neither; other members, `access_token` among them, are ignored); `sign_in` (optional): true; `cancel` (optional): true; `sign_out` (optional): true; `export` (optional): true | `{"state":"in\|pending\|out","name":"…","photo":"https://…","source":"tube\|own","code":"…","url":"…","error":"…"}`: the YouTube account the engine is signed in to (`name` and `photo` are "" when signed out, `photo` is a url to pass to `thumbs`). `import` is what the UI passes on from the Tube app's card: the engine refreshes the token once to prove it (and finds YouTube TV's own client from its page when none came), asks the account's name, and only then is signed in; a failed import leaves the previous account as it was. `sign_in` starts the engine's own sign-in and answers at once with `state` `pending`: the engine asks Google for a code, `code` and `url` are then filled (the user enters the code at `url`, on any device), and the engine polls until the code is entered, refused or expired, ending signed in with `source` `own` or out with `error` (`access_denied`, `expired_token`, a network or service message). A sign-in already running is replaced. `cancel` ends a running sign-in (out, no `error`; nothing when none runs). `sign_out` forgets the account, ends a running sign-in and clears `error`; the token is also revoked at Google, best effort and in the background, whether it is our own or came from the Tube app's card: the Tube app is then signed out too. `state` is `pending` while a sign-in runs, even when an account is still signed in; `code`, `url` and `error` are "" when they do not apply. `export` answers the account's members plus `refresh_token`, `client_id`, `client_secret`, for the UI's sign-in card to hand to the app the user allowed (`signed_out` without an account; no network; nothing changes). More than one of `import`, `sign_in`, `cancel`, `sign_out` and `export` in a request is `bad_request`; `network`: YouTube could not be reached; `refused`: Google does not accept the token. The token is echoed back by `export` only. The account is kept across restarts. The bare form only reports. |
| `repeat` | `mode`: `off`, `all` or `one` | `{}`. `all` goes back to the first track after the last; `one` plays the current track again when it ends (Next still moves on). Kept across restarts. |
| `quit` | | `{}`, then the engine exits |

Only songs, videos and episodes can be played or queued; an Item of another kind gets `code`
`bad_request`, as does a missing or wrongly typed field. An index outside the queue gets
`not_found`. `network` means YouTube could not be reached or answered with an error. `refused` (`account` only) means Google no longer accepts the token. `signed_out` means the command needs an account and there is none (or Google refused the token and the account was signed out).

In `status`, `index` is -1 when the queue is empty; `stopped` still carries the current track.
After a restart the queue comes back paused, and its tracks have lost `explicit`,
`playlist_id` and `params` (the store keeps no columns for them) and `kind`: an episode comes back as a
song, its `artist` and `album_id` still the show's.

Commands answer without waiting for the sound: `seek`, `next` and the rest are acknowledged
before anything changes, and what happens next comes as events. The exceptions are the ones
that ask YouTube (`search`, `search_more`, `suggest`, `radio`, and `play` with `radio`).

A search for `all` that YouTube answers as one flat list comes back grouped by kind, in the
sections "Top result", "Songs", "Albums", "Artists", "Videos", "Playlists", "Podcasts" (shows) and
"Episodes" (singles and EPs are albums; the empty ones are left out). A podcast show that is the top
result comes as a shelf without a title: it is the section "Top result" too.

Every change to the queue (`play`, `radio`, `queue_add`, `queue_remove`, `queue_move`, `queue_clear`,
`queue_shuffle`, `queue_sort`, `shuffle`, a radio page arriving) is followed by a `queue` event, and every `track` event by a `position` event.

## Events

| `ev` | Fields | When |
|---|---|---|
| `state` | `state`, `index`, `track_id`, `position_ms`, `duration_ms`, `error` | on every change of state |
| `track` | `index`, `track`: Track | the current track changed, also at a gapless boundary |
| `position` | `position_ms`, `duration_ms` | about once a second while playing |
| `queue` | `len`, `index` | the queue's content changed; ask `queue` for it |
| `error` | `message`, `track_id` | a track could not be played; the player has moved on |
| `mode` | `repeat`, `shuffle`, `eq` | the repeat mode, the shuffle mode or the equalizer's preset changed; all three are always there |
| `region` | `gl` | `region` was set with a `gl` (not when the engine detected it): content already shown is of the old country |
| `account` | `state` (`in`, `pending` or `out`), `name`, `photo`, `source`, `code`, `url`, `error` | on every change: a sign-in started, its code arriving, its end (signed in, refused, expired, cancelled), a sign-out, an import, also when a refresh is refused (the account is then `out`) |
| `favorite` | `id`, `on`, `offline`, `count` | after every `favorite` that sets (`offline` false then), and when a favourite's download completes (`on` and `offline` true); `count` is how many favourites there are |
| `skipped` | `category`, `from_ms`, `to_ms`, `track_id` | a segment of the playing track was skipped |
| `thumb` | `url`, `size`, `file` | a thumbnail asked for with `thumbs` is in the cache; `url` and `size` are the request's |

A favourite is offline when its track is whole in the song cache: it then plays with no network. Favourites come from the store, so they are as a restored queue's tracks are: kind `song`, no `explicit`, `playlist_id`, `params`, `plays` or `year`. A download starts when a favourite is added (and at engine start for the ones not yet offline), one track at a time in the background.

A UI that connects late sends `status` and `queue` and is then up to date.
