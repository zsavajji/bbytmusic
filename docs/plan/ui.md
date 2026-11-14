# BBMusic plan: the UI — handover

For the agent who picks up the UI. Written 2026-11-14, when the first working build
(`bbmusic.bar` 0.1.0.7) was on the user's Q10 and the user had said the look comes next, from
screenshots they will provide. Read [PLAN.md](../../PLAN.md) §3 and §7 first, then
[docs/protocol.md](../protocol.md): the UI speaks that protocol to the engine and does nothing
else. [phase1.md](phase1.md) ends with what each build showed on the phone.

## What the user wants

Their words (2026-11-14): "i would like this, and the player in particular to resemble the
stock player with a Youtube flavor, but with the functions the UI has on Windows phone. i will
provide screenshots".

- **Model for layout and feel:** BB10's own music player.
- **Identity:** YouTube Music's colours and flavour on top of it.
- **Functions:** what YTMusicWP's UI can do (its screenshots are in the upstream repo's
  `Pictures/screenshots/`; `git clone --depth 1 https://github.com/Yasukoisreal/YTMusicWP`).
- **The mini-player** is the title and artist plus one Play/Pause icon. "only play / pause
  should be there, other actions can be in the full player". The buttons are icons "to fit".
- **An info view for the current track** (type, year and the like) "as an option".
- **Wait for the screenshots** before reworking the look, and measure from them rather than
  guess: the Q10 is 720 × 720, 9 px per design unit, OLED (dark theme), with a keyboard.
  Screenshots the user takes on the phone land in `/accounts/1000/shared/camera/IMG_*.png`;
  copy them into `debug/` (git-ignore it) and never commit them.

What is in `app/` today is a first functional shell, written to prove the engine end to end.
Its structure can stay or go; nothing about its look was designed.

## What exists

```
app/
├── bar-descriptor.xml     id com.zsavajji.bbmusic; the UI entry point and the headless engine
│                          (bbmusicd, invoke target com.zsavajji.bbmusic.service, action …START)
├── bbmusic.pro            a plain executable, Qt 4.8 + Cascades
├── src/
│   ├── main.cpp           registers QTimer as `Timer` (import bbmusic 1.0); context properties
│   │                      `engine` and `player`
│   ├── EngineLink.h/.cpp  starts the engine, reads data/engine.json, connects on loopback, says
│   │                      hello with the token; `engine.request(cmd, fields)` returns the id;
│   │                      signals `reply(id, ok, result, errorCode, errorMessage)` and
│   │                      `event(name, fields)`; reconnects by itself; after hello it sends
│   │                      `status` and `queue` and re-emits their replies as the events
│   │                      `status` and `queue_snapshot`
│   ├── PlayerState.h/.cpp properties for QML: state, playing, loaded, index, title, artist,
│   │                      album, positionMs (advanced by a timer between events), durationMs,
│   │                      error, queueLength; `fmt(ms)`
│   └── UiLog.h            appends to data/bbmusic-ui.log
└── assets/
    ├── main.qml           TabbedPane: Search, Queue; menu: Stop, About; the error toast
    ├── SearchPage.qml     text field with focus, suggestions (300 ms debounce), filter DropDown,
    │                      results by section, tap = play with radio, context actions, More
    ├── QueuePage.qml      the queue, the playing row marked, tap = jump, Remove, Play next
    ├── NowPlayingPage.qml title, artist, album, scrubber, times, Previous / Play-Pause / Next
    │                      as text buttons; Space, N, P
    ├── MiniPlayer.qml     title + artist (tap opens Now Playing) and one ImageButton
    ├── ConnectionBar.qml  "the engine is not connected" + Retry
    └── images/            ic_play.png, ic_pause.png (81 px, drawn by scripts/gen-icons.py)
```

No file is over 370 lines; keep each under about 500, one page or class per file (the user's
rule).

## Build, install, look

```sh
scripts/build-app.sh                      # engine (Docker, bbmusic-build) + UI (Docker, yamsergey/bb10-ndk:0.6)
SKIP_ENGINE=1 scripts/build-app.sh        # UI only, reusing build/engine-bb10/bbmusicd
scripts/install-phone.sh build/bbmusic.bar   # the Q10 over root ssh (bb10rootx); prints result::success
```

- Each build gets the next build id; check `Package-Version` in the script's output against
  what you tell the user.
- Lint every QML file before building:
  `../hyperberry/.claude/skills/bb10-build-deploy/scripts/cascades-qmllint.sh app/assets/*.qml`.
  Exit 255 means the app would quit at start. Known false positives: `Timer` (registered from
  C++) and functions declared on a `ListView` and called from its items.
- The logs, in `/accounts/1000/appdata/com.zsavajji.bbmusic.testRel*/data/` on the phone:
  `bbmusic-ui.log` (every line sent and received, except positions and the hello) and
  `bbmusicd.log` (every command, state and track change). Read them before changing code.
- The phone: the user allowed reading and writing over ssh for tests, and installing test
  builds this way. No reboots, no system settings. `/root/bbmusic` is our scratch folder.
- The engine alone, on the Mac: `scripts/test-engine.sh`, then
  `build/engine-host/bbmusicd --data /tmp/x --null` and talk to it with the token in
  `/tmp/x/engine.json` (a UI change that needs a new command can be tried there first).

## Read before writing QML

`../hyperberry/.claude/skills/`: `bb10-cascades`, `bb10-pitfalls`, `bb10-ux`, `bb10-devices`,
`bb10-build-deploy`. And `../hyperberry/docs/plan/pitfalls.md`, which is longer. The ones that
already bit here or will:

- Qt 4.8 and GCC 4.6.3: C++98, `SIGNAL`/`SLOT`. QML is `bb.cascades`; no QtQuick visual items;
  JavaScript is ES5. A QML error quits the app at start with nothing on screen.
- **A control's "changed" signal also fires when code changes it.** The scrubber sent a seek
  on every `valueChanged` and restarted the song whenever the page opened. Act on a finger
  (`onTouch`), not on a value.
- **Two `Button`s at their preferred widths can take a whole row.** Give the part that must
  stay tappable its own `spaceQuota`.
- The UI makes **no network request**: Qt 4.8 cannot speak modern TLS. That includes artwork
  (see below).
- `ActionItem` has no `visible`; `Slider` has both `value` and `immediateValue`; list items
  live in a context of their own and reach the page through `ListItem.view`.

## How the user tests

- They test fast and report in one line ("works, plays stops scrubs"; "pause never becomes
  play"). Match each report to the two logs before touching code.
- "please check them all, and fix, build only once after fix": batch fixes, one build.
- Say exactly what a build contains. 0.1.0.2 had a placeholder engine and was taken for a
  working build.
- A build or probe that needs their finger must be checked first as far as it can be without
  them (the engine can be driven on the phone from the Mac: phase1.md, "On the Q10").

## What works on the phone (0.1.0.7), and what has not been tried

Works, by the user's report and the logs: the engine starts with the app and connects;
suggestions and search; a tap plays with a radio queue of 50; the mini-player; Now Playing
opens without disturbing playback; pause, resume and the scrubber; the queue restored paused
after a restart; playback from the cache in under 0.2 s.

Not tried by the user yet: the filter and More on Search; context actions; the Queue page's
jump, Remove and Play next; Next and Previous; one song running into the next; the keyboard
shortcuts; the menu; the volume overlay's buttons with this build (they worked with the gate
app, and the engine now holds now-playing itself); swiping the app away mid-song and coming
back; an error toast with Wi-Fi off.

Open questions from testing: "first play request did not pass, second worked" on 0.1.0.4,
unexplained and not seen since; an underrun line in the engine's log after some resumes and
seeks, not known to be audible.

## What the UI needs next, in the plan's order

The steps are PLAN.md §9; what each needs from the engine is noted, because several need a
command that does not exist yet. A new command goes into `docs/protocol.md` first, then the
engine (`engine/src/engine/commands.cpp`), then the UI.

| Step | UI | Engine side |
|---|---|---|
| 2.5 Artwork | list rows and the player show covers | **missing:** fetch `Item.thumb`, cache it under `data/thumbs/`, and give the UI a file path (a command, or a `thumb_file` field filled in when cached) |
| 2.6 Now Playing | the redesign from the screenshots; icons for Previous/Play/Next; the info view | none for the layout; year and type label would need `Item` fields |
| 2.8 Queue | reorder by drag if the design wants it | there (`queue_move`) |
| 2.9 Overlay | nothing in the UI | there; artwork in the overlay untried (`albumArtwork`, a `file://` path) |
| 2.10 Active Frame | a cover with title and state, updated only while thumbnailed | none |
| 2.11 Resume and errors | what the pages show while loading, with no network, for an unplayable track | events are there; check the wording of `error` messages |
| 2.12 Measure | — | memory, CPU, battery per hour, tap-to-sound |
| 3.1 Home | carousels | **missing:** `browse` `FEmusic_home` and its parser |
| 3.2 Album, playlist | a page each; today a tap says "Not yet" | **missing:** `browse` for `MPRE…` and `VL…` |
| 3.3 Artist | top songs, albums, singles | **missing:** `browse` for a channel id |
| 3.4 Explore | charts, moods and genres | **missing** |
| 3.5 Item actions | play next, add to queue, start radio, go to artist/album, share | mostly there; "go to" needs 3.2 and 3.3 |
| 3.6 History | a page; opening links from other apps (an invoke target) | the store has `history`; **missing:** a command to read it |
| 3.7 Region and language | a setting | `InnerTube::set_language` exists; **missing:** a command and a stored setting |
| 3.8 SponsorBlock, 3.9 Equalizer | settings sheets | **missing** entirely |

The renderers the missing `browse` pages return are parsed for search already
(`engine/src/innertube/items.cpp`); YTMusicWP's `InnerTubeClient.Browse.cs` is the map for the
rest.

## Not yours to change without asking

The protocol's framing, the engine's module contracts (`docs/plan/phase1.md`), and anything
under `engine/` beyond adding a command the UI needs. The user's delegation rules for
sub-agents are in their global instructions; for this project they allowed one Opus and two
Sonnet agents when working unattended, each in folders of its own, with the main session
reviewing and committing every step.
