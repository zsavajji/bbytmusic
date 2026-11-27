# BBMusic plan: lyrics

Written 2026-11-27. It takes item 6.1 of `PLAN.md` ("Lyrics: YouTube and LRCLIB, the LRC parser,
the synced view") out of "not for now". Modelled on YTMusicWP (`Pages/MainPage.Lyrics.cs`,
`Services/LrcParser.cs`, `Services/*LyricsApi.cs`, read at its GitHub head on 2026-11-27).

## What the user decided (2026-11-27)

- "activatable in now playing through a button alongside favorites / repeat, it would replace the
  album cover with the lyrics."
- "you may then enable autosync or just lyrics there scrollable."

## What YTMusicWP does

One function, `UpdateLyricsAsync`, tries in this order and stops at the first that answers:

1. Apple Music TTML through a third-party proxy (`lyrics-api.boidu.dev`), keyed by title, artist
   and duration (±2 s, else 401). Word-by-word.
2. KuGou (`mobileservice.kugou.com` search, then `lyrics.kugou.com` KRC download). Word-by-word,
   strong on Asian music.
3. LRCLIB: `/api/get?track_name&artist_name&duration` (exact), then two `/api/search` (title +
   artist, and free text), the result nearest in duration winning. Synced (LRC) or plain.
4. YouTube's captions of the video, as a last resort.

It cleans the title first ("(Official Video)", "(feat. …)", "Remix", "Artist - Title" from a
video's title), caches the last results in memory (LRU), adds a "Lyrics provided by …" line, and
draws word-by-word karaoke with gradients and animations (about 2 000 of its 2 400 lines).

It does **not** use YouTube Music's own lyrics tab.

## What we take, and why

Probed from the Mac on 2026-11-27 (two songs, anonymous `WEB_REMIX` and the mobile clients):

- **YouTube Music's own lyrics.** `next` already gives the tab's browse id (`MPLYt…`;
  `next.cpp` parses it into `lyrics_browse_id`, unused so far). `browse` on it with `WEB_REMIX`
  answers about 3 kB: `sectionListRenderer.contents[0].musicDescriptionShelfRenderer`,
  `description.runs[0].text` the plain lyrics, `footer.runs[0].text` the credit ("Source:
  Musixmatch", "Source: LyricFind"). **Plain only.** `ANDROID_MUSIC` and `IOS_MUSIC` answer
  `timedLyricsData`, but 636 kB per song and, anonymously, without the times: not worth it.
- **LRCLIB.** `/api/get` with title, artist and duration answered the probe song with
  `syncedLyrics` (LRC, line times to 10 ms) and `plainLyrics`. Free, no key, no account.
- Left out, as `PLAN.md` §10 already says: KuGou and Apple (word-by-word, a third-party proxy for
  Apple, the karaoke drawing that Cascades cannot do cheaply), and captions (rarely lyrics on
  music videos).

So: **LRCLIB for synced lines, YouTube Music for plain text**, two sources, at most three
requests per song.

## The engine

A command, in `docs/protocol.md`:

| `cmd` | Fields | `result` |
|---|---|---|
| `lyrics` | `id`: a video id | `{"id":"…","synced":true,"lines":[{"ms":18830,"text":"…"}],"credit":"Source: LRCLIB"}`. `synced` false: `ms` is 0 on every line. `credit` is what to show under the last line ("Source: LRCLIB", or YouTube's footer: "Source: Musixmatch"). `not_found` when neither source has the song. |

The order:

1. LRCLIB `/api/get` with the cleaned title, the artist and `duration_s`. Synced: done.
2. YouTube: `next` for the id (the browse id is not kept from earlier), then `browse` on
   `MPLYt…`. Text: done, plain.
3. LRCLIB's plain text from step 1, if it had some. Else `not_found`.

No `/api/search` to start: `/api/get` matched both probe songs. Add it if the user finds
misses on songs that have lyrics on lrclib.net.

Where the title and artist come from: the track store (every queued track is in it). A
`video` (not a `song`) gets the title cleaning (the bracketed "Official Video", "Lyric Video",
"Audio", "MV", "Visualizer", "feat."; "Artist - Title" when the artist is the channel). A song
from YouTube Music's own catalogue is already clean.

Files (each well under 500 lines):

- `engine/src/lyrics/lrc.{h,cpp}`: the LRC parser (`[mm:ss.xx]`, several stamps on one line,
  `[offset:]`, metadata tags skipped, sorted by time) and the title cleaner. Pure functions.
- `engine/src/lyrics/lrclib.{h,cpp}`: the `/api/get` call through `net/http`.
- `engine/src/innertube/lyrics.{h,cpp}`: the `MPLYt…` browse and its parse.
- the command's wiring in `engine/` as the other network commands are.
- Tests under `engine/tests/`: the parser and the cleaner as plain cases; the two answers as
  fixtures. **The fixtures hold made-up lines, not the song's**: the repository is public and
  lyrics are copyrighted.

The answers are not cached to start: one song's lyrics are asked once per opening of the view,
and the UI keeps the last one. See decision 3 for offline.

Privacy: LRCLIB sees the title, artist and duration from the phone's address, but only when the
user opens the lyrics (a tap on the button), never in the background. SponsorBlock, which asks
for every track, was made off by default for that reason; lyrics need no such setting.

## The UI (`NowPlayingPage.qml`)

- **The button.** An `ImageButton` in the row with Favorite / Repeat / Shuffle (the album row),
  dim when off, lit when on, as those are. A new icon `ic_lyrics.png` drawn by
  `scripts/gen-icons.py` (lines of text with a note). The row has room for one more 8 du button on
  the Q10's 720 px; on a long album title the label shrinks, as now.
- **Replaces the artwork.** In the artwork's `DockLayout`, a `ListView` over the `ImageView`,
  visible when lyrics are on; the artwork is hidden (or dimmed under a dark layer: to try). The
  scrubber stays over the lower edge, so the list gets a bottom padding as tall as the scrubber.
  The state stays on across songs: the next song's lyrics are asked when it starts; while they
  come, an `ActivityIndicator`; `not_found` shows "No lyrics for this song".
- **Autosync** (synced lyrics only). The current line white and larger, the others grey; the list
  scrolls to keep it near the top third. The position comes once a second (`position` event), but
  `PlayerState` already advances `positionMs` every 250 ms between events, so no timer of its own.
- **Free scroll.** A finger on the list turns autosync off; a small "Sync" toggle in the list's
  top-right corner turns it back on (and is the way to turn it off without scrolling). Plain
  lyrics have no toggle: they only scroll.
- **Tap a line to seek** (synced only), as YTMusicWP does. One `seek` request.
- Strings in the 9 translations.

Untested on Cascades and to check on the phone: binding a list item's colour to the page's
current index (`ListItem.view`), and `scrollToItem` keeping the line visible without jumping
(it aligns to the top: we scroll to the line two above the current one).

## Decisions

The user said "go." on 2026-11-27 to the defaults: LRCLIB first for synced lines, YouTube Music
for plain text; the Sync toggle in the lyrics area; no offline lyrics for favourites for now;
tap a line to seek. Built as 1.0.3 (the user: "bump version to 1.0.3 before building and
installing").

Known limit: the UI has one connection to the engine and the engine answers its commands in
order, so a `lyrics` request (LRCLIB up to 10 s, then YouTube) delays a Pause tapped meanwhile,
as `search` and `browse` already do.

## Steps

1. Engine: `lrc` + tests, `lrclib`, `innertube/lyrics`, the `lyrics` command,
   `docs/protocol.md`. Checked on the Mac with `scripts/test-engine.sh` and a run against the two
   probe songs.
2. UI: the icon, the button, the list over the artwork, autosync, free scroll, seek;
   translations. Lint with `cascades-qmllint.sh`, a read-only sub-agent review of the QML against
   the qmltypes (as for every page so far).
3. One build, installed when the user is at the phone; they judge the look.

## On the phone (2026-11-27)

Tried by the user on the Passport: the lyrics load, the sync follows, a tap seeks right. From
their screenshot of 1.0.3.85 they asked for the lines left-aligned and bold, more space between
them, the Sync button on a row of its own, and the list stopping above the scrubber (it ran on
under it); all in 1.0.3.87, which they approved ("good!").
