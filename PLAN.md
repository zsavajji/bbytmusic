# BBMusic: YouTube Music for BlackBerry 10

A native YouTube Music client for BlackBerry 10, with the feature set of
[YTMusicWP](https://github.com/Yasukoisreal/YTMusicWP) (Windows Phone 8.1) as the target, and
HyperBerry's shape — a Cascades UI and a headless engine in one `.bar` — with the engine in
C++20 instead of Go (§3).

This file is the plan: what we build, why each big choice went the way it did, the phases in
order, and the questions only the user can answer. Anything marked **(verify)** is unconfirmed
and has a gate in Phase 0 or a note in §11.

**Status:** draft 4, written 2026-11-14. Phase 0 is under way, with its results in
[docs/plan/phase0.md](docs/plan/phase0.md). The steps in §9 are agreed and the questions are
answered (§14). Draft 1 had a Go
engine; the user asked why, and the answer didn't hold up (§3). Draft 2 put a C++20 engine
inside the UI's process; the user wants music to survive closing the app, so it is a headless
process again. Sources read: YTMusicWP at its
`main` (2.4.0), `../bbtube` at `f3beb28`, `../hyperberry` (PLAN.md, docs/plan/design.md,
pitfalls.md, phase0.md), `../bb10-cxx20`, `../docs`.

**Where things stand, and where the rest of the plan lives:**
[docs/plan/status.md](docs/plan/status.md) (start there), [phase0.md](docs/plan/phase0.md) (the
gates), [phase1.md](docs/plan/phase1.md) (the engine), [ui.md](docs/plan/ui.md) (the UI
handover), [docs/protocol.md](docs/protocol.md) (UI ↔ engine).

---

## 1. Iteration 1 in one paragraph

One `.bar` with two processes, as in HyperBerry. **`bbmusicd`** is the engine: a headless program
in C++20 built with bb10-clang++, no Qt. It speaks InnerTube to `music.youtube.com` over
libcurl + wolfSSL, resolves a track to a stream, fetches it over **DASH ranges or SABR/UMP**,
decodes **Opus** with libopus and writes PCM to libasound itself, so music goes on when the app
is closed. It also holds the system's now-playing service, so the play, pause, next and
previous buttons of the volume overlay work with the app closed. **`bbmusic`** is the UI:
Cascades/QML on Qt 4.8, which owns the screen and the Active Frame, and talks to the engine
over loopback. Iteration 1 is: search, play a song with seek, a queue with radio continuation, a Now
Playing page, and playback that continues with the app minimised or closed. No login.

## 2. What the two references give us

### YTMusicWP (C#/XAML, GPLv3) — the feature and protocol reference

It can't be reused as code (WinRT, C# 6), but it is a complete, current map of the protocol.
What we port, by size in its tree:

| Part | Files | Lines | Fate |
|---|---|---|---|
| InnerTube client (`WEB_REMIX` browse/search/next, player clients) | `Services/InnerTubeClient*.cs` | 5 700 | port; the renderer paths are the value |
| SABR/UMP audio | `AudioPlayerTask/SabrMediaStreamSource.cs`, `UmpParser.cs`, `MiniProtoWriter.cs` | 2 300 | port, generalised from itag 140 to any format |
| Queue, radio, playback state | `AudioPlayerTask/AudioTask.cs` | 3 350 | redesign; most of it works around WP's 20 MB background task |
| Lyrics (YouTube, LRCLIB, KuGou, Apple TTML) | `Pages/MainPage.Lyrics.cs`, `Services/*Lyrics*`, `LrcParser.cs` | 3 400 | deferred (6.1) |
| SponsorBlock, Return YouTube Dislike, Last.fm | `Services/*` | 600 | SponsorBlock in step 3.8; the others deferred |
| Listen Together (WebSocket + protobuf) | `Services/ListenTogether/*`, page | 3 300 | later or never (§10) |
| Unit tests (92) | `YTMusicWP.Tests/*` | 1 900 | port the vectors: UMP, proto writer, LRC, format choice, SponsorBlock |
| UI | `MainPage.xaml` + pages | 12 000 | reference for screens only |

Its player-client chain, in order: `ANDROID` 20.49.37, `ANDROID` 21.02.35, `WEB_REMIX` with
cookies, `VISIONOS` 1.02, `ANDROID_VR` 1.65.10, `WEB_REMIX` + poToken, `VISIONOS` + poToken, then
a Piped instance. Two things in it we do **not** copy: the poToken comes from the author's own
Cloudflare Worker (a third party in every playback), and SABR is wired to itag 140 with 44.1 kHz
frame maths hardcoded.

### bbtube (C++03 Cascades, Apache-2.0) — proof of what works on the phone, not a base

It proves, on a real BB10 device today: InnerTube `/player` answers these clients (`TVHTML5`,
`ANDROID_VR` 1.65.10, `ANDROID`), Google sign-in completes in the Cascades `WebView` and its
cookies can be captured, SAPISIDHASH auth is accepted, `NowPlayingConnection` and
`run_when_backgrounded` do what a music app needs.

What we leave behind, and why:

- **Its player.** It downloads video-only + audio-only files in chunks and stitches MP4s on disk
  because "mmrenderer only opens complete local files"; audio-only mode plays the remote URL but
  has **seek disabled**. That is 4 700 lines (`ChunkedRemuxSession`, `StreamingRemuxSession`,
  `mp4_*remux`) around a constraint we remove by not using mmrenderer for music.
- **Invidious first.** Direct playback routes through public Invidious instances and only then
  YouTube. A music app can't depend on them.
- **No SABR, no PO token, `WEB` client only** for browse: nothing of `music.youtube.com`.
- **UI in hand-written C++03, networking through Qt 4.8** (HyperBerry measured it as TLS 1.0
  only on the Q10; bbtube carries its own root-CA patch).

Kept as reference: the client table with its dated notes, `GoogleLoginSheet` (the WebView flow),
`applyAuthHeaders` (the three SAPISID hashes), `audio_manager` routing events for unplugged
headphones.

## 3. Architecture

```
                          one .bar, one sandbox (same uid, same data/)
 ┌──────────────────────────────────┐             ┌──────────────────────────────────────────┐
 │ bbmusic  (UI, Cascades/QML)      │             │ bbmusicd  (engine, headless entry point) │
 │ qcc 4.6.3 · Qt 4.8               │  127.0.0.1  │ C++20 · bb10-clang++ · static libc++     │
 │                                  │  TCP+token  │                                          │
 │ EngineLink (QTcpSocket, NDJSON) ─┼─────────────┼─► server: commands in, events out        │
 │ QML pages ◄─► C++ models         │             │   innertube ─► stream (resolve,          │
 │ Active Frame                     │             │     DASH | SABR/UMP) ─► segment cache    │
 │ WebView sign-in                  │             │   player: queue, WebM demux, libopus,    │
 │                                  │             │     equalizer, gapless ─► libasound      │
 │                                  │             │   now-playing: PPS, the overlay's buttons│
 │                                  │             │   store: SQLite (data/music.db)          │
 │                                  │             │   net: libcurl + wolfSSL + roots         │
 └──────────────────────────────────┘             └──────────────────────────────────────────┘
```

### Why the engine is its own process

The user wants music to go on after the app is swiped away (answer 5, §14). On BB10 that means
a headless entry point in the same `.bar`, and the speaker has to belong to it. So the engine
is a process, started by the UI through an invoke target, as HyperBerry's is.

This also removes a risk draft 2 carried: a libc++ library loaded into a Cascades process built
with GCC 4.6. A standalone program is what bb10-cxx20 is made for.

What it brings back from HyperBerry, all measured there on the Q10: a headless heap is 3 MB
unless the entry point holds `_sys_headless_xlarge`; the process must reach the navigator (BPS)
within 6 seconds of launch or it is slain; any app can reach loopback, so the engine wants a
token only this sandbox can read. BPS is a C library, so here it is a direct call.

**When the engine stops:** it exits by itself after ten minutes with nothing playing and no UI
connected. It does not start at boot.

### Why C++ and not Go

Draft 1 copied HyperBerry's Go engine. HyperBerry's engine is Go because its protocol library is
Go; there is no Go library we need.

- What Go would bring — TLS 1.3, an HTTP client, JSON, development on the Mac — C++ gets from
  wolfSSL, libcurl and yyjson. The Mac loop belongs to "an engine with no Qt in it", not to Go.
- What Go would cost: a 15.6 MB binary (HyperBerry's, measured) against 3–4 MB (the gate 0.3
  probe with libcurl, wolfSSL and the C++ runtime is 2.4 MB stripped); a
  garbage-collected heap inside a headless memory class, where running out kills the process;
  cgo for libopus, libasound, BPS and SQLite; a dependency on the go-qnx fork.
- What C++ costs: parsers of network bytes without memory safety (UMP, WebM, protobuf, JSON).
  The Mac build runs every test under ASan and UBSan, and the three binary parsers get a fuzz
  target.

### TLS and HTTP

- **wolfSSL**, static, client-only build: TLS 1.3 and 1.2, a few hundred KB, QNX among its
  supported targets, a supported libcurl backend, GPL (compatible with this project's GPLv3;
  exact version **(verify)**). OpenSSL 3 would also work and is the fallback if wolfSSL
  misbehaves on QNX 6.6, at several times the size.
- **libcurl**, static, HTTP only, built against wolfSSL: connection reuse, redirects, chunked
  bodies, gzip, timeouts, streaming response bodies for UMP. The SDK's own libcurl and OpenSSL
  are too old and are not used.
- **Roots:** Mozilla's bundle compiled in and handed to curl as a blob. BB10's store is from
  2013.
- Nothing goes to Google through Qt 4.8 or mmrenderer.

### UI ↔ engine

NDJSON over loopback TCP, one message per line, as HyperBerry's `docs/protocol.md`: commands
from the UI, events from the engine, ids as strings. The token is a file in `data/` written by
the engine at start. On the Mac the same engine listens on the same port and is driven with
`socat`. The UI reconnects to a running engine and asks for the current state, which is how an
app reopened mid-song shows the right track.

### Data

The engine is the only code that opens `data/music.db`; the UI asks for rows over the socket.
HyperBerry lets its UI read the database directly so that chats show before the engine is up.
We start without that — one owner of the schema — and add it only if a cold start feels slow.

## 4. The streaming engine

One interface, `Source`, yields encoded audio packets with timestamps and supports `seek(ms)`.
Two implementations sit behind it; the player doesn't know which it got.

### 4.1 Resolve

`/youtubei/v1/player` per client, in a table with dated notes (as bbtube's). Each attempt is
classified: direct URLs present → DASH; `serverAbrStreamingUrl` + `ustreamerConfig` only → SABR;
neither → next client. The order, from gate 0.1 (2026-11-14, anonymous, no PO token):

1. `VISIONOS` 1.02 — direct URLs for Opus 251 that keep serving, and SABR too
2. `IOS`, `ANDROID` — SABR only
3. `WEB_REMIX` with cookies, when signed in — refuses without a PO token or a JS player; needed
   for Premium bitrates, private uploads, age-restricted tracks (a Phase 4 question)

`ANDROID_VR`, which bbtube and the earlier drafts put first, now answers 403 after the first
64 KB, and every web client refuses an anonymous request. The matrix is in
[docs/plan/phase0.md](docs/plan/phase0.md).

### 4.2 Fetch

- **DASH (ranged adaptive).** The player response gives `initRange` and `indexRange` per format.
  Fetch the init and the index (WebM Cues for Opus, `sidx` for AAC), then media by byte range in
  bounded chunks. Seek is an index lookup. Simple, and the path most plays will take while direct
  URLs last.
- **SABR.** `POST serverAbrStreamingUrl&rn=N&ump=1` with a `VideoPlaybackAbrRequest` protobuf
  (`ClientAbrState` with `player_time_ms` and audio-only track type, `selected_format_ids`,
  `buffered_ranges`, `ustreamer_config`, `preferred_audio_format_ids`, `streamer_context` with
  client info, PO token, playback cookie); the response is UMP. Parts handled: `MEDIA_HEADER`
  20, `MEDIA` 21, `MEDIA_END` 22, `NEXT_REQUEST_POLICY` 35 (backoff, playback cookie),
  `FORMAT_INITIALIZATION_METADATA` 42, `SABR_REDIRECT` 43, `SABR_ERROR` 44, `SABR_SEEK` 45,
  `RELOAD_PLAYER_RESPONSE` 46, `STREAM_PROTECTION_STATUS` 58, `LIVE_METADATA` 31. Field numbers
  come from YTMusicWP's `MiniProtoWriter`; cross-check against LuanRT/googlevideo's `.proto`
  files and yt-dlp before trusting them **(verify)**. Protobuf is written and read by hand
  (varints and length-delimited fields, about 150 lines): no codegen, no library.

Both write packets into the same segment cache on disk, keyed by video id and itag.

### 4.3 Decode: Opus, in the engine

Request **itag 251** (WebM/Opus, ~160 kbps; 774 for Premium) and decode with static libopus
(HyperBerry's build recipe: `--disable-rtcd`, pinned by sha256). WebM demux for one audio track
is a small EBML reader. PCM is 48 kHz stereo s16le.

Owning the decoder is what makes this better than both references:

- **Seek** to any position, on either source.
- **Gapless** albums: the next track is decoded into the same PCM stream. Crossfade is the same
  mechanism, deferred (6.4).
- A real **equalizer** (step 3.9: biquads on PCM; YTMusicWP could not have one). Loudness
  normalisation from the player response's `loudnessDb` is deferred (6.3).
- No container rewriting, no mmrenderer quirks, one code path for streaming and offline.

AAC (itag 140/141) is not decoded in iteration 1. It matters for livestreams, which rarely offer
Opus; see §10.

### 4.4 Output

The engine's audio thread writes PCM straight to libasound, opened through `audio_manager` as
multimedia audio (routing to speaker, headset and Bluetooth is the system's). Position is what
has been written to the device. Whether a headless process may open the speaker is **gate 0.4**.

**Now-playing is the engine's** (gate 0.5, 2026-11-14). BB10 has no player on the lock screen;
the controls are in the volume overlay, fed by `/usr/sbin/nowplaying` through the PPS object
`/pps/services/multimedia/mediaplayer/control`. `bb::multimedia::NowPlayingConnection` is only
a client of it, and dies with the UI. The engine speaks the same messages itself — `register`,
`acquire`, `metadata`, `state` out; `track` with `pause`, `play`, `next`, `prev` in; `revoke`
when another player takes over — so the buttons work with the app closed. The overlay draws
the state it is told, so the engine sends `state` on every change. The messages are in
[docs/plan/phase0.md](docs/plan/phase0.md). The headset button is not covered by this and has
its own check in step 2.9.

### 4.5 Cache and downloads

The segment cache is the offline store. A "download" is a fetch to the end, pinned. Export to a
tagged `.opus`/`.m4a` in `shared/music` is a later step (YTMusicWP's `M4aMetadataWriter` is the
reference).

### 4.6 PO tokens

Needed for some SABR sessions, never for the DASH path. BotGuard needs a modern JS engine, which
the phone's WebKit is not. Options, in order of preference: none (stay on clients that don't need
one); a self-hosted provider (bgutil) on the user's own box, URL in settings; on-device later if
bb10-webkit can run it **(verify)**. We don't ship a default pointing at someone else's server.

### If the PCM path fails its gate

Fallbacks keep the same `Source` layer and change only the sink: (a) the engine serves the track
to mmrenderer over loopback HTTP as HLS with ADTS or MPEG-TS segments; (b) the engine writes a
complete `.m4a` and mmrenderer plays the file, as bbtube does. Both lose gapless and the EQ.

## 5. InnerTube surface (`WEB_REMIX`)

| Screen | Endpoint | Notes |
|---|---|---|
| Home | `browse` `FEmusic_home` + continuations | carousels: quick picks, mixes, 16:9 videos |
| Explore, charts, moods | `FEmusic_explore`, `FEmusic_charts`, `FEmusic_moods_and_genres(_category)` | |
| Search | `search` + params per filter, `music/get_search_suggestions` | songs, videos, albums, artists, playlists |
| Artist, album, playlist | `browse` channel id / `MPRE…` / `VL…` | |
| Queue, radio | `next` (`RDAMVM…`, `RDAMPL…`) | also returns the lyrics and related browse ids |
| Library | `FEmusic_liked_playlists`, `FEmusic_library_*`, `VLLM` | signed in |
| Like, subscribe | `like/like`, `like/removelike`, `subscription/*` | signed in |
| Playlists | `playlist/create`, `playlist/delete`, `browse/edit_playlist` | signed in |
| Accounts | `account/account_menu` | |

All of it is parsing a handful of renderers (`musicResponsiveListItemRenderer`,
`musicTwoRowItemRenderer`, `musicCarouselShelfRenderer`, `musicShelfRenderer`) with yyjson. The
engine turns them into one small item shape — `{kind, id, title, subtitle, thumb, duration, …}`
— and that is all the UI ever sees.

## 6. Auth

**Superseded on 2026-11-20 by [docs/plan/phase4.md](docs/plan/phase4.md):** a TV device-code
sign-in, shared with the Tube app, after gate 0.7b showed it opens the library. What follows is
the plan as first written.

Cookie login only. The UI opens a Cascades `WebView` on Google sign-in continuing to
`music.youtube.com`, reads the cookie jar (bbtube's `GoogleLoginSheet`), and passes the cookies
to the engine, which stores them and signs requests with SAPISIDHASH / 1PHASH / 3PHASH for origin
`https://music.youtube.com`. YTMusicWP's device-code (QR) login is not ported: it yields a TV
token that doesn't open the Music library. If the 2013 WebView stops completing Google's sign-in,
the fallback is importing cookies exported from a desktop browser.

## 7. UI

**The look is the user's to shape** (2026-11-14): "i would like this, and the player in
particular to resemble the stock player with a Youtube flavor, but with the functions the UI
has on Windows phone. i will provide screenshots". So: BB10's own music player as the model for
layout and feel, YouTube Music's colours and identity on top, and YTMusicWP's functions as the
list of what the screens must be able to do. The UI in `app/` today is a first functional
shell, not the design; wait for the screenshots before reworking it, and measure from them
(the Q10 is 720 × 720, 9 px per du). The mini-player is the title plus one Play/Pause icon;
every other action is on the full player (the user, same day).

QML on C++ models, HyperBerry's split. Designed for the Q10's 720×720 and keyboard first, then
tall screens.

- `TabbedPane`: Home, Search, Library (+ Settings in the application menu)
- Pages: artist, album/playlist, queue, Now Playing (artwork, scrubber, like, lyrics, queue)
- A mini-player bar on every page; type-to-search and keyboard shortcuts (space, n/p, l, s)
- Active Frame: artwork, title, state
- Artwork is fetched and cached by the engine; QML shows files

Not ported from YTMusicWP's look: the blurred Apple-Music backdrop, motion artwork, per-word
lyric animation, Live Tiles. Cascades has no blur, and the rest costs more than it gives on this
hardware. `../hyperberry/docs/plan/pitfalls.md` applies in full.

## 8. Data (`data/music.db`, opened by the engine only)

`tracks` (id, title, artists, album, duration, thumb), `playlists`, `playlist_tracks`, `library`
(liked, saved albums/artists), `queue` (position, track, source), `history` (track, played_at,
ms played — also feeds stats later), `downloads` (track, itag, bytes, state), `settings`,
`session` (cookies). Numbered migrations with `PRAGMA user_version`. The engine links the
phone's own SQLite, 3.7.14.1 (the user, 2026-11-14): no SQL newer than that, so no upsert.

## 9. Phases

Each phase ends on the phone. Each gets its own task-level plan when it starts.

### Phase 0 — gates (cheapest first; a failed gate changes the plan, not the code)

| Gate | Where | Question | Pass |
|---|---|---|---|
| 0.1 | Mac | What does `/player` return per client today? | the matrix of §4.1, with at least one no-token client giving itag 251 |
| 0.2 | Mac | Do both fetchers work? | one track by DASH ranges and the same track by SABR decode to WAVs of the same length; seek to 2:00 on both |
| 0.3 | Q10 | Does a C++20 headless engine run and reach Google? | `bbmusicd` in a `.bar` beside a minimal Cascades app: started through its invoke target, reaches the navigator in time, token handshake over loopback, a TLS 1.3 request to `music.youtube.com` through libcurl + wolfSSL, the result shown in QML |
| 0.4 | Q10 | Can the headless engine play, with the app closed? | a track plays through libasound for 10 min, screen off, **app swiped away**: no underruns, the decoder under ~15 % of a core, memory inside the headless class |
| 0.5 | Q10 | Who answers the lock screen and the media keys? | with the app minimised: `NowPlayingConnection` works with no `MediaPlayer`. With the app closed: a written-down answer to what still works and whether the engine can do it itself |
| 0.6 | Q10 | Routing and interruptions | headphones out pauses; a phone call interrupts and resumes; Bluetooth plays |
| 0.7 | Q10 | Does WebView sign-in reach `music.youtube.com` with cookies? | `FEmusic_liked_playlists` returns the user's playlists from the engine |

If 0.4 fails because a headless process can't open the speaker, the speaker moves to the UI
(PCM over a second loopback connection, as HyperBerry's calls do) and music stops when the app
is closed: tell the user before going on. If it fails on decoding cost, take the sink fallback
in §4.

**The steps below are agreed** (user, 2026-11-14), with their changes: Phases 0–4 as proposed;
SponsorBlock and the equalizer moved into Phase 3; Phase 5 cut down to translations and
hardening; the rest of the old Phase 5 deferred to Phase 6.

### Phase 1 — the engine on the Mac

Driven with `socat` and a WAV sink; every step lands with its tests.

- 1.1 Skeleton: CMake, the NDJSON server on loopback, the message loop, tests under ASan/UBSan
- 1.2 `net`: libcurl + wolfSSL wrapper (GET/POST, streamed bodies, gzip, bundled roots)
- 1.3 InnerTube core: `WEB_REMIX` context, visitor data, the request helper, the item shape
- 1.4 Search: results per filter, continuations, suggestions
- 1.5 Resolve: the player-client table, DASH/SABR classification, format choice (port
  YTMusicWP's `FormatSelector` tests)
- 1.6 WebM demux + libopus, to a WAV
- 1.7 DASH source: init and index ranges, ranged fetch, seek
- 1.8 Protobuf writer/reader and the UMP parser (port YTMusicWP's test vectors)
- 1.9 SABR source: the request loop, next-request policy and backoff, redirect, seek,
  protection status, player-response reload
- 1.10 Segment cache on disk, with a fixed size cap (making it a setting is deferred, 6.6)
- 1.11 Player: queue, play/pause/seek, position events, prefetch of the next track, gapless
- 1.12 `next`: the watch queue and radio continuation
- 1.13 Store: SQLite, migrations, the queue and history persisted
- 1.14 Fuzz targets for UMP, WebM and protobuf; a script that records fixtures

### Phase 2 — iteration 1 on the phone

- 2.1 Cross-build: engine and third-party libraries in Docker, both binaries in the `.bar`, the
  headless entry point, `EngineLink` (QTcpSocket and the token), the engine's idle exit
- 2.2 The libasound sink and `audio_manager` routing events (from gates 0.4 and 0.6)
- 2.3 App shell: `TabbedPane`, theme, QML lint in the build
- 2.4 Search page: type-to-search, suggestions, results by kind
- 2.5 Artwork: the engine's thumbnail cache, list items
- 2.6 Now Playing: artwork, scrubber, controls
- 2.7 Mini-player bar on every page
- 2.8 Queue page: reorder, remove, radio continuation
- 2.9 Now-playing in the engine over PPS (metadata, artwork, state, the overlay's buttons,
  `revoke`); the headset button (`audio_manager` button events, untried)
- 2.10 Active Frame
- 2.11 Resume and errors: the queue and position restored at launch; no network; unplayable
  track
- 2.12 Measure: memory, CPU, battery per hour, time from tap to sound

### Phase 3 — browse

- 3.1 Home: the carousels and their continuations
- 3.2 Album and playlist page
- 3.3 Artist page: top songs, albums, singles, related
- 3.4 Explore: charts, moods and genres
- 3.5 Item actions: play next, add to queue, start radio, go to artist/album, share
- 3.6 History page; opening `music.youtube.com` and `youtu.be` links from other apps
- 3.7 Region and language
- 3.8 SponsorBlock: skip sponsor, intro, outro and non-music segments; a switch per category
- 3.9 Equalizer: biquad bands on the PCM stream in the engine, presets, a settings sheet

### Phase 4 — account

Its steps are now the ones in [docs/plan/phase4.md](docs/plan/phase4.md) (2026-11-20); the list
below is the first plan's.

- 4.1 Sign-in sheet (WebView), cookie capture, sign-out
- 4.2 Engine auth: the cookie store, the SAPISID hashes, signed requests, the signed-in home
- 4.3 Library: playlists, liked songs, albums, artists, synced into `music.db`
- 4.4 Like, unlike, subscribe
- 4.5 Playlists: create, rename, delete, add, remove
- 4.6 Signed-in playback through `WEB_REMIX` (SABR) and Premium quality; PO tokens as gate 0.1
  decided

### Phase 5 — finish

- 5.1 Translations: English, Italian
- 5.2 Hardening: Wi-Fi ↔ mobile handover, expired stream URLs, hours-long sessions

### Phase 6 — not for now

Deferred by the user on 2026-11-14 (6.1–6.6 were the old Phase 5); none is planned.

- 6.1 Lyrics: YouTube and LRCLIB, the LRC parser (port the tests), the synced view
- 6.2 Downloads: pin a track, an album, a playlist; offline mode; the storage page
- 6.3 Loudness normalisation
- 6.4 Crossfade
- 6.5 Sleep timer
- 6.6 Settings: quality on Wi-Fi and on mobile data, cache size
- 6.7 Last.fm
- 6.8 Listening stats
- 6.9 Export of tagged files to `shared/music`
- 6.10 A PO-token provider
- 6.11 Livestreams (AAC: our own decoder, or mmrenderer through HLS)
- 6.12 Multiple accounts

## 10. Scope

**In:** Phases 0–5 as listed in §9. **Later, if wanted:** Phase 6 (lyrics, downloads,
normalisation, crossfade, sleep timer and the rest), KuGou/Apple lyrics, word-by-word lyrics,
pinning to the home screen. **Not planned:** Listen Together (a
second protocol and someone else's servers), the Samples video feed (video is bbtube's job),
motion artwork, DNS over HTTPS (revisit only if an ISP blocks), Piped or Invidious fallbacks.

## 11. Risks

- **YouTube moves.** Direct URLs may disappear for the no-token clients; then SABR is the only
  path and PO tokens become necessary for more sessions. This is why both sources exist from
  Phase 1 and why the client table is data, not code.
- **SABR field numbers and `FormatId`.** YTMusicWP only ever asked for itag 140 by itag alone.
  Opus by SABR may need `last_modified`/`xtags` in `FormatId` **(verify, 0.2)**.
- **Audio from a headless process** **(verify, 0.4)**. HyperBerry kept the speaker in the UI;
  nobody here has opened libasound from a headless entry point. If it can't be done, music
  stops when the app is closed.
- **The now-playing messages are undocumented.** They were read out of the service's own
  program and checked on the Q10 on 10.3.3; another OS version may differ.
- **The headset button with the app closed** is untried, as are a phone call and Bluetooth
  during playback (the user could not test them).
- **An engine that never stops** would drain the battery, hence the idle exit (§3).
- **wolfSSL and libcurl on QNX 6.6** — both claim QNX; neither has been built with bb10-clang
  **(verify, 0.3)**.
- **Software Opus decode on the phone** — CPU and battery **(verify, 0.4)**. HyperBerry decodes
  Opus on the Q10 already, at voice rates.
- **The 2013 WebView and Google sign-in.** Works in bbtube today; Google can end that any time.
- **Terms of service.** An unofficial client, as HyperBerry is for WhatsApp. No ads are fetched
  and nothing is rehosted; the README says so.
- **Licence.** Porting YTMusicWP's logic makes this GPLv3, as HyperBerry is. bbtube's Apache-2.0
  code can be quoted into a GPLv3 project with its notices.

## 12. Size

Rough, in the units HyperBerry's phases turned out to be: Phase 0 is small (Mac work plus two
evenings on the Q10, one of them the build of wolfSSL, libcurl and libopus); Phase 1 is the
largest single piece (the SABR port and the InnerTube parsers); Phases 2–3 are mostly QML;
Phase 4 is small once 0.7 has passed; Phase 5 is two small pieces. Iteration 1 is Phases 0–2.

## 13. Repo layout

```
bbmusic/
├── PLAN.md                 this file; docs/plan/ once it outgrows 500 lines
├── docker/Dockerfile       bb10-cxx20 + wolfSSL, libcurl, libopus, pinned by sha256
├── scripts/                build-engine.sh, test-engine.sh (Mac: ASan/UBSan), build-app.sh
├── engine/                 bbmusicd: CMake, C++20, no Qt; builds for the phone and the Mac
│   ├── src/                server/, net/, innertube/, stream/ (resolve, dash, sabr, ump, proto),
│   │                       media/ (webm, opus), player/, audio/ (qsa, wav), store/, bps/
│   ├── tests/              unit tests, fixtures, fuzz targets
│   └── third_party/        yyjson, SQLite amalgamation (fetched, pinned)
├── app/                    Cascades UI: bbmusic.pro, src/, assets/*.qml, bar-descriptor.xml
├── docs/protocol.md        UI ↔ engine messages
└── .claude/skills/         imported from YTMusicWP's .agents/skills (2026-11-14)
```

## 14. The user's answers (2026-11-14)

1. **A C++20 engine, not Go:** yes. They also said yes to running it inside the Cascades
   process, but answer 5 rules that out; the engine is a headless process (§3).
2. **Opus decoded by us, PCM to libasound, no mmrenderer:** yes.
3. **PO tokens:** none to start; a self-hosted provider only if gate 0.1 shows signed-in
   playback needs one.
4. **Reference device:** the Q10 on 10.3.3.
5. **Music survives swiping the app away:** yes.
6. **Extras:** SponsorBlock and the equalizer, in Phase 3. Lyrics, downloads, normalisation,
   crossfade, sleep timer and the quality/cache settings are not needed for now (Phase 6).

The go-ahead for Phase 0 came the same day ("go continue", then "the Q10 is connected, please
work unattended"). On the phone that was taken to mean: programs pushed over `ssh bb10rootx`
into `/root/bbmusic`, a scratch folder of our own, and nothing installed. Installing a `.bar`
stays with the user unless they say otherwise, as in HyperBerry.
