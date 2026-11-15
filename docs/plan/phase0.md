# BBMusic plan: Phase 0, the gates

Part of [PLAN.md](../../PLAN.md) §9. One section per gate, with what was run and what came back.

## 0.1 What `/player` returns per client — passed, 2026-11-14 (Mac)

`python3 tools/gates/player_matrix.py --out <dir>`: anonymous, no PO token, visitor data from a
`WEB_REMIX` search. Two videos, same answers on both: `dQw4w9WgXcQ` (a music video) and
`lYBUbBu4W08` (an art track found through search).

| Client | Version | Status | Audio with direct url | SABR | Bytes at 0 / at 1 MB |
|---|---|---|---|---|---|
| `VISIONOS` | 1.02 | OK | 139, 140, 249, 250, **251** | yes | 206 / 206 |
| `ANDROID_VR` | 1.65.10 | OK | 139, 140, 249, 251 | yes | 206 / **403** |
| `ANDROID` | 21.26.364 | OK | none | yes | — |
| `IOS` | 21.26.4 | OK | none | yes | — |
| `TVHTML5` 7.x and 5.x, `WEB`, `MWEB`, `WEB_REMIX` | | UNPLAYABLE, "The page needs to be reloaded." | | | |
| `WEB_EMBEDDED_PLAYER` | | ERROR, "This video is unavailable" | | | |

What it means:

- **`VISIONOS` is the first client:** direct urls for Opus 251 that keep serving past the first
  megabyte, and SABR as well, with no PO token.
- **`ANDROID_VR` is no longer usable for direct urls.** The first 64 KB come, then 403. This is
  the client bbtube and draft 1–3 of the plan put first; yt-dlp's table says the same since
  2026-08-17.
- **`ANDROID` and `IOS` are SABR-only** now. They are the fallbacks if `VISIONOS` goes.
- **Every web client refuses an anonymous request** without a PO token or a JS player. Signed-in
  playback through `WEB_REMIX` (step 4.6) will need one or the other; that is a Phase 4 question.
- PLAN.md §4.1's order is replaced by: `VISIONOS` (DASH, then SABR), `IOS`, `ANDROID` (SABR).

## 0.2 Both fetchers — passed, 2026-11-14 (Mac)

`python3 tools/gates/fetch_probe.py --out <dir>`: `dQw4w9WgXcQ`, itag 251, resolved through
`VISIONOS`; decoded with ffmpeg.

| | DASH ranges | SABR/UMP |
|---|---|---|
| Requests | 5 (head + 1 MB chunks) | 22 (one 10 s segment each) |
| Time | 1.0 s | 3.6 s |
| Bytes | 3 433 755 | 3 433 755 |
| Decoded length | 213.043 s | 213.043 s |
| Seek to 2:00 | first packet at 109.994 s | first packet at 109.994 s |

The two WebM files are byte-identical, and so are the WAVs. A seek lands on the 10-second
cluster that contains the target; the player drops the samples before it.

What the SABR port (steps 1.8, 1.9) must know, found here:

- **WebM media headers carry no `start_ms` or `duration_ms`** (fields 11, 12 are 0). Time is in
  `time_range` (field 15: start ticks, duration ticks, timescale). YTMusicWP reads 11 and 12,
  which works for its itag 140 only. With them at 0 the client reports nothing buffered and the
  server sends segment 1 forever.
- **`FormatId` needs `last_modified`** (field 2) beside the itag; `xtags` was absent for this
  format.
- Field numbers checked against LuanRT/googlevideo's `.proto` files: request field 2 is
  `initialization_format_ids`, 16 is `selected_audio_format_ids`.
- The server sent **one segment per request** with `target_audio_readahead_ms` 15 000 and no
  backoff. A download advances `player_time_ms` to the end of what it holds; a player sends its
  real position and gets about 15 s ahead.
- `STREAM_PROTECTION_STATUS` was 1 (fine) on every request of the 213 s track, with no PO token.
- A SABR "init segment" for WebM is the file's first bytes up to the end of the Cues, and
  segment N is cluster N, so init + segments in order is the same file DASH gives.
- Parts seen and safe to ignore: 47 playback start policy, 49 bandwidth sampling hint,
  51 selectable formats, 52 request identifier.

Not covered: a track longer than a few minutes, a second request after the url's expiry, a
signed-in session.

## 0.3 A C++20 engine on the phone that reaches Google — first half passed, 2026-11-14 (Q10)

What ran: `tools/gates/gate03.cpp`, built with `bb10-clang++` in the `bbmusic-build` image
(`docker/Dockerfile`: wolfSSL 5.9.4, curl 8.23.0, both static), pushed with
`scripts/push-phone.sh` to `/root/bbmusic` and started from a root shell over ssh. Not yet run:
the same code as a headless entry point in a `.bar`, started through its invoke target.

```
exception                    ok    thrown with 100 bytes alive
std::thread                  ok    value 42
libcurl                      ok    8.23.0 with wolfSSL/5.9.4
TLS 1.3 to music.youtube     ok    http 200, 757274 bytes, 1.88 s
search (WEB_REMIX)           ok    http 200, 239114 bytes, visitor data yes, 1.10 s
/player (VISIONOS)           ok    http 200, 86441 bytes, playability OK, itag 251 url yes, SABR url yes, 0.57 s
64 KB of itag 251            ok    http 206, 65536 bytes, 0.48 s, first bytes 1a45dfa3
```

TLS 1.3 is forced in the probe (`CURLOPT_SSLVERSION`), so each `ok` is a 1.3 handshake made by
wolfSSL on the phone, checked against the Mozilla bundle pushed beside the program.

Found on the way:

- **`-lz` breaks C++ exceptions.** The phone's `libz.so.2` exports a libgcc unwinder
  (`_Unwind_*`), as its libc and libm do. bb10-cxx20 puts the program's own libraries before
  its C++ runtime on the link line, so with `-lz` the linker binds the runtime's unwinder calls
  to libz, and the first `throw` dies with SIGSEGV in
  `libz.so.2@_Unwind_GetLanguageSpecificData`. Link `libz.a` by its path instead; the check is
  `readelf --dyn-syms -W <binary> | grep -ci ' UND .*unwind'`, which must print 0. libsocket,
  libasound, libaudio_manager and libbps export no unwinder. Worth a fix in bb10-cxx20's driver.
- **The Q10 had no network of its own** during the run (`/pps/services/networking/status_public`:
  `ip_ok::error_not_up`, no name servers). The probe went through
  `tools/gates/connect_proxy.py` on the Mac, reached with `ssh -R 8899:127.0.0.1:8899`: a
  CONNECT tunnel, so TLS is still end to end from the phone. **Name resolution on the phone is
  therefore not checked**: rerun `gate03 cacert.pem` with Wi-Fi on.
- A plain GET of `music.youtube.com` is redirected to `consent.youtube.com` (EU). The InnerTube
  calls don't care.
- Size: the probe is 3.0 MB, 2.4 MB stripped, with libcurl, wolfSSL and the C++ runtime inside.
  wolfSSL's `--enable-curl` set is most of it and can be trimmed later.
- curl's CMake build has no `HTTP_ONLY` any more: each protocol is switched off by its own
  `CURL_DISABLE_*` (the Dockerfile lists them).

## 0.4 Opus through libasound — passed from a shell, 2026-11-14 (Q10); not yet as a headless app

What ran: `tools/gates/gate04.cpp` (WebM walk, libopus 1.6.1, libasound opened through
`audio_manager` as multimedia audio), pushed to `/root/bbmusic` and started from a root shell
over ssh. The track is ten minutes of pink noise plus a 440 Hz tone, Opus at about 110 kbps,
played at −50 dB because nobody was at the phone.

| | |
|---|---|
| Speaker | `pcmPreferred`, 48 000 Hz, 2 voices, fragment 4096 bytes × 9 |
| 600 s of audio | 600.1 s of wall time, **0 underruns**, 0 write errors, 0 decode errors |
| Whole process | 34.8 s of CPU, **5.8 % of a core** |
| Decoder alone, flat out | 620 s of audio in 7.9 s: **78× real time** |
| Decoder alone, while playing | 10× real time: the phone clocks the CPU down between packets |

What this does and doesn't settle:

- The decode cost and the libasound path are fine, with a wide margin under the 15 % bar.
- **It ran as root from a shell.** Whether a headless entry point of an installed app may open
  the speaker, and whether it keeps playing with the app swiped away, is what the gate app
  below is for. The screen's state during the run isn't known.
- Nobody listened: "no underruns" is the device's own status, not an ear.

Found on the way:

- **No libm call per sample.** The gate engine first scaled samples with `std::lrint` and
  measured 18.6 % of a core for the same playback; with an integer multiply and shift it is
  5.7 %. Keep the sample path in integers (the equalizer of step 3.9 too).
- `std::clock()` under-counts beside the decoder's own wall time (35 s of CPU against 58 s
  spent inside `opus_decode`), so read these percentages as "a few per cent", not as exact.
- `bps_initialize()` and `navigator_request_events()` succeed from a root shell too, so
  "navigator connected" in a log is not proof of a launch by the navigator.
- `scripts/push-phone.sh` removes older copies named `<program>-*`: a test file called
  `gate04-noise.webm` was deleted by pushing `gate04`. Name data files otherwise.

## The gate app: `build/bbmusic-gate.bar` (0.0.1.3) — built, not installed

`scripts/build-gate.sh` builds it from `gateapp/`: a headless engine `bbmusicd-gate` (C++20:
navigator thread, NDJSON server on loopback with a token in `data/engine.json`, commands `net`,
`play`, `stop`, `status`, `quit`, a log in `data/gate.log`) and a one-page Cascades UI that
starts it through its invoke target. It is for the gates that need an installed app; the user
installs it.

Checked without installing: the engine's own `selftest` on the Q10 from a shell (wrong token
refused, hello, status, 15 s of playback through the speaker with 0 underruns, events,
`audio_done`, quit), QML lint, and Qt 4.8's `lupdate` on the QML. **Never run:** the UI, the
headless launch, `NowPlayingConnection`, the `net` command.

The test list, in order (the phone needs Wi-Fi for step 2):

1. Install the `.bar`, open **BBMusic Gate**, press **Start engine**. Expect "engine started",
   then "connected" within 5 s. *(0.3: headless launch, navigator, token.)*
2. **Network test**. Expect `"http":200` or a redirect code with `"error":""`.
   *(0.3: TLS 1.3 and name resolution on the phone's own network.)*
3. **Play 10 min, audible**. Expect noise with a tone, and an `audio` line every 5 s with
   `"underruns":0`. Lock the screen for a minute, unlock, check the lines went on. *(0.4)*
4. While it plays, **swipe the app away** and listen: does it go on? Open the app again, press
   **Start engine**, then **Status**: `"playing":true` and `played` still rising means the
   engine outlived the UI. *(0.4, answer 5)*
5. **Lock-screen controls on**. Expect "acquired". Lock the screen: is there a player with
   "BBMusic gate / Test tone"? Press pause there and on a headset button: the log should show
   `pause` and the sound stop; play starts the quiet tone. *(0.5, app minimised)*
6. With music playing, swipe the app away again and try the lock screen and the headset button.
   Whatever happens is the answer to "who holds the controls with the app closed". *(0.5)*
7. While it plays: pull the headphones out; take a phone call; connect a Bluetooth speaker.
   Note what the sound does each time. *(0.6; the engine does nothing about any of these yet)*
8. **Stop**, then swipe the app away. The engine has no idle exit: it stays until the phone
   restarts or the app is removed.

Afterwards the logs are `data/gate.log` (engine) and `data/gate-ui.log` (UI) in the app's
sandbox, `/accounts/1000/appdata/com.zsavajji.bbmusic.gate.*/data/`.

## Still open after this session

- 0.3: the headless launch, and name resolution on the phone (steps 1–2 above).
- 0.4: playing as a headless app with the app closed (steps 3–4).
- 0.5 and 0.6: need a person at the phone (steps 5–7).
- 0.7: WebView sign-in needs the user's Google account; nothing is built for it yet.

Left on the phone, all under `/root/bbmusic`: the probe binaries (`gate03-*`, `gate04-*`,
`bbmusicd-gate-*`), `cacert.pem`, `noise-opus.webm`, `test.webm`, `gate.log`. Nothing else was
written, and nothing was installed.

## Later on 2026-11-14: Wi-Fi up, gate app installed

- **Name resolution on the phone works.** With the Q10 on its own Wi-Fi (`bcm0`, one name
  server), `gate03 cacert.pem` passed every line from a root shell with no tunnel: TLS 1.3 to
  `music.youtube.com` in 1.85 s, search, `/player`, 64 KB of itag 251.
- The user installed `bbmusic-gate.bar` 0.0.1.3 and opened it (`gate-ui.log`: "ui started",
  13:05:33). The engine had not been started yet when this was written; the test list above is
  theirs to run.

## The gate app on the Q10, first round (0.0.1.3) — 2026-11-14, run by the user

The user's report: "1 2 3 4 ok. 5 saw no lockscreen controls but when i change the volume i saw
the playpayse button. pause worked fine, play did not resume, even if status said yes, i could
hear no audio. also, play never became visible, it stayed the pause button. headphones worked,
but only after i stopped and restarted." Steps 6 and 8, and the call and Bluetooth parts of 7,
were not reported. Read against `gate.log` and `gate-ui.log` copied from the app's sandbox:

**0.3 passed in full.** The engine was started by the navigator through the invoke target
(`argv … invoke://localhost`), reached it in 3 ms, the UI connected with the token within a
second, and `net` returned `http 200` twice in about 1.5 s on the phone's own Wi-Fi.

**0.4 passed in full.** One stretch of 157 s played with 0 underruns at 5.6 % of a core, and
it ran through the app being swiped away (`client gone` 13:08:25, a new client at 13:08:37).
A headless entry point may open the speaker, and it outlives the UI. The speaker stays in the
engine.

**0.5, with the app open or minimised:**

- `NowPlayingConnection` works with no `MediaPlayer`: `acquire()` returned 0, `acquired`
  arrived, and `pause` and `play` reached the app.
- **BB10 shows no player on the lock screen.** The controls are in the volume overlay, which
  appears when a volume key is pressed.
- **The overlay draws the state it is told, and sends the opposite.** The gate app set
  `Started` once and never `Paused`, so the button stayed "pause" and the events came out of
  step (three `pause` in a row, then `play` four times while already playing). The UI must set
  the media state on every change.
- **"Play did not resume" was the test's fault, not the engine's.** The overlay's play started
  the tone at −40 dB by design (mine), after the user had been listening at −15 dB. The engine
  log shows the speaker opened and 11.6 s written with no error. Not heard is not the same as
  not played, so 0.0.1.4 resumes at the last gain and this gets checked again.
- A second `acquire()` while holding the connection returns 3.

**0.6, headphones:** a stream stays on the output it was opened on. Plugging in or pulling out
during playback changed nothing until a stop and a start. The engine has to watch the output
device and open the speaker again; and by convention stop when headphones come out.

**Not yet known:** the controls with the app closed (step 6), a phone call, Bluetooth.

Also seen: the engine process is still there after the last stop (no idle exit in the gate
app), and `cpu_pct` reads 23 % for the first seconds after a start, then settles near 6 %.

## Gate app 0.0.1.4 — built, waiting for the user

Three changes, each for a finding above:

1. The UI tells the overlay `Started` or `Paused` on every play, stop, and when playback ends.
2. The overlay's play resumes at the gain last played.
3. The engine reads `audio_manager_get_default_device()` about twice a second while playing.
   On a change it logs `route: A -> B`; if headphones, a headset or Bluetooth gave way to the
   speaker it stops, otherwise it closes and reopens the speaker and plays on.

Checked before handing over: the engine's selftest on the Q10 from a shell (8 s, 0 underruns),
QML lint. The route code has never seen a route change.

What to try: (a) play audible, press a volume key, pause, then play from the overlay: the
glyph should flip each time and the sound come back; (b) while playing on the speaker, plug
headphones in: sound should move to them within a second; pull them out: it should stop;
(c) with music playing, swipe the app away and press a volume key: is the play/pause button
still there, and does it do anything? (d) a phone call during playback; (e) a Bluetooth
speaker connected during playback.

A candidate for the controls with the app closed, not built: `audio_manager` has
`audio_manager_add_device_button_change_event()`, a C interface the engine could use for the
headset button without Qt.

## The gate app, second round (0.0.1.4) — 2026-11-14, run by the user

The user's report: "1 2 works, 3 does not work, 4 and 5 i can't test". Against the logs:

- **The overlay's play and pause now work** (a): each `pause` is followed by `state Paused`,
  each `play` by `state Started`, they alternate, and the sound came back. A `previous` event
  arrived too.
- **Output switching works** (b): `route: speaker -> headphone (reopening)` 0.6 s after the
  device's own status went to underrun, then the stream went on; `headphone -> speaker` and
  `headset -> speaker` both stopped playback. One underrun is counted at each switch.
- **With the app closed there are no controls** (c). The music goes on (16 s across a
  swipe-away in this round), but `NowPlayingConnection` dies with the UI, so the volume
  overlay has no player. The only way to stop the music is to open the app again.
- A phone call and Bluetooth (d, e): the user can't test them. Still unknown.

## 0.5, continued: the engine can hold now-playing itself

`bb::multimedia::NowPlayingConnection` is a client of a PPS service, `/usr/sbin/nowplaying`,
at `/pps/services/multimedia/mediaplayer/control`. `tools/gates/gate05.cpp` talks to it from
a plain process with no Qt (run from a root shell). What the service accepted, each answered
with `res::<msg>` and the same `id`, or `err::badparam`:

```
msg::register
id::1
dat:json:{"name":"bbmusic-probe","prio":"low","audio":"general","overlay":"fancy","pid":<pid>}

msg::acquire
id::2

msg::metadata
id::3
dat:json:{"title":"…","artist":"…","album":"…","duration":600000,"position":0,"nextEnabled":true,"prevEnabled":true}

msg::state
id::4
dat::playing          (also paused, stopped; a JSON object here is refused)
```

After `acquire`, `/pps/services/multimedia/mediaplayer/status` showed the probe as
`active_name`, with its pid. How the keys were found: the public library's strings give the
message names; the service's own program (copied off the phone, read with `strings`) gives the
keys it parses, and its log (`slog2info`, lines of `nowplaying.<pid>`) says which one it
missed. `audio` is required and undocumented; without it the answer is `badparam`.

Not yet seen: the overlay's buttons arriving in such a process (the library's strings name
`play`, `pause`, `next`, `previous`, `stop`, `revoke`), and album artwork. If the buttons
arrive, now-playing moves from the UI into the engine and the controls survive the app being
closed.

**The four-minute listener (2026-11-14):** the probe held now-playing for the whole time
(`active_name::bbmusic-probe` at the end) and received one message, `msg::track` /
`dat::sendData`, which the library's strings pair with "controller active": something asked
for the track's data. No `play`, `pause`, `next` or `previous` arrived. The user had not said
whether they pressed anything in that window, so this neither shows that the buttons arrive
nor that they don't. Rerun with the user at the phone:
`gate05 240 <register> <acquire> <metadata> <state>` as above.

**The seven-minute listener (2026-11-14):** the user pressed a volume key and reported "it said
probe artist": the overlay shows the metadata a plain process sent. The service's log has the
overlay subscribing (`controller md_subscribe`) and `sendData to cid … bbmusic-probe`, which is
the `msg::track` / `dat::sendData` the probe received. Nothing for play, pause, next or
previous, in the probe or in the service's log, which records volume-key presses in detail;
whether the user tapped those buttons, or whether the overlay showed them, was not yet known
when this was written.

## 0.5 settled: the overlay's buttons reach a plain process — 2026-11-14 (Q10, with the user)

Fourth run of `gate05`, the gate app closed, the user at the phone: "ok it flips, pressed all
of them". What the probe received on the control object, and what it answered:

```
msg::track / dat::sendData      the overlay opened: it wants the track's data
msg::track / dat::pause         → the probe wrote msg::state / dat::paused   (res::state)
msg::track / dat::play          → msg::state / dat::playing                  (res::state)
msg::track / dat::prev
msg::track / dat::next
msg::track / dat::holdData      the overlay closed
msg::revoke / dat:json:{"pid":"<pid>","name":"<name>"}     another player acquired
```

So the buttons arrive as `msg::track` with the command in `dat`, the glyph follows the `state`
the player writes back, and the overlay shows the title and artist from `metadata`. Everything
`NowPlayingConnection` did for the gate app, a process with no Qt can do; **now-playing moves
into the engine**, and the controls survive the app being closed. PLAN.md §3, §4.4 and step 2.9
say so now.

Two bugs in the probe cost three rounds with the user, both mine: it stopped listening 300 ms
after the first message (so opening the overlay ended it), and it matched `msg::pause` where
the service sends `dat::pause`.

Not looked at: album artwork (`albumArtwork` in the library's strings, a `file://` path), the
position, what `prio` `high` changes, the `gid` and `audioman_handle` registration parameters,
and whether a headless app's process is treated like the root shell's.

## Phase 0 at the end of 2026-11-14

| Gate | |
|---|---|
| 0.1 `/player` per client | passed |
| 0.2 DASH and SABR | passed |
| 0.3 headless C++20 engine, TLS 1.3 | passed |
| 0.4 Opus to libasound from the headless engine, app closed | passed |
| 0.5 controls | passed with the app open (overlay, through the UI) and, from a root shell, with no Qt at all; not yet from the headless engine itself |
| 0.6 routing and interruptions | headphones passed; a phone call and Bluetooth untested |
| 0.7 WebView sign-in | not started |

## 0.7b The TV sign-in (device code) against music.youtube.com — 2026-11-15 (Mac, with the user)

The user: "i know for a fact that the tv flow works in the Tube app, so i would likely mirror
it". Probe (not committed; this session's scratch `tv/tvauth.py`): YouTube's TV web app's own
OAuth client, taken from its JavaScript (`www.youtube.com/tv`, the kabuki base js: `clientId`,
its secret, `scopes: "http://gdata.youtube.com https://www.googleapis.com/auth/youtube-paid-content"`;
the older `youtube-paired-clients` scope is now `invalid_scope`). `POST
www.youtube.com/o/oauth2/device/code` gives a code for google.com/device; the user entered it;
`/o/oauth2/token` (grant `http://oauth.net/grant_type/device/1.0`) gave an access token (~22 h)
and a refresh token. With `Authorization: Bearer`:

| Client, host, browse | Answer |
|---|---|
| WEB_REMIX, music.youtube.com: VLLM, FEmusic_liked_playlists, FEmusic_home | 400 "Request contains an invalid argument" (Home too, which works without a token) |
| ANDROID_MUSIC (music.youtube.com, youtubei.googleapis.com), IOS_MUSIC | 400, the same |
| TVHTML5, music.youtube.com: FEmusic_liked_playlists | 200, logged in: the music library, in TV renderers (`tvBrowseRenderer`, `tvSurfaceContentRenderer`, tabs) |
| TVHTML5, music.youtube.com / www.youtube.com: VLLM | 200, logged in: "Liked Music" (0 songs for this account) |
| TVHTML5, www.youtube.com: FEplaylist_aggregation, FElibrary | 200, logged in: the account's playlists (`tileRenderer`) |
| TVHTML5, music.youtube.com: FEmusic_home | 400 |

So: a TV sign-in gives the library, playlists and Liked Music through the TV client, in TV
renderers (a second parser), and very likely likes and playlist edits (the TV app does them);
not a personalised music Home, which only WEB_REMIX serves and which refuses the TV token.
Playback is untouched (VISIONOS, no sign-in). Untried: likes and edits with the TV client; a
Google Cloud OAuth client of the user's own (ytmusicapi's way to WEB_REMIX with a token;
refresh tokens expire after 7 days while such a project is in testing); cookies from a WebView
(gate 0.7).
