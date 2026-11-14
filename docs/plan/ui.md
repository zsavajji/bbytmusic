# BBMusic plan: the UI — handover

For the agent who picks up the UI. Written 2026-11-14 with the first working build
(`bbmusic.bar` 0.1.0.7) on the user's Q10, and rewritten that evening after an unattended
session that built the look from the user's screenshots (0.1.0.9 to 0.1.0.22). The user first
opened one, 0.1.0.19, that evening, and has been sending remarks since. Read [PLAN.md](../../PLAN.md) §3 and §7 first, then
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
- **Measure from screenshots** rather than guess: the Q10 is 720 × 720, 9 px per design unit,
  OLED (dark theme), with a keyboard. Screenshots the user takes on the phone land in
  `/accounts/1000/shared/camera/IMG_*.png`; copy them into `debug/` (git-ignored) and never
  commit them. The ones the design below was measured from are in `debug/shots/`.

## What exists (0.1.0.22, 2026-11-14)

```
app/
├── bar-descriptor.xml     id com.zsavajji.bbmusic; the UI entry point and the headless engine
│                          (bbmusicd, invoke target com.zsavajji.bbmusic.service, action …START);
│                          theme dark with YouTube's red as the primary colour
├── bbmusic.pro            a plain executable, Qt 4.8 + Cascades
├── src/
│   ├── main.cpp           registers QTimer as `Timer` (import bbmusic 1.0); context properties
│   │                      `engine`, `player` and `thumbs`; with data/check-qml present it
│   │                      compiles every QML document at start and logs the result
│   ├── EngineLink.h/.cpp  starts the engine, reads data/engine.json, connects on loopback, says
│   │                      hello with the token; `engine.request(cmd, fields)` returns the id;
│   │                      signals `reply(id, ok, result, errorCode, errorMessage)` and
│   │                      `event(name, fields)`; reconnects by itself; after hello it sends
│   │                      `status` and `queue` and re-emits their replies as the events
│   │                      `status` and `queue_snapshot`; a request in flight when the link
│   │                      drops gets a failed reply with the code `disconnected`
│   ├── PlayerState.h/.cpp properties for QML: state, playing, loaded, index, title, artist,
│   │                      album, positionMs (advanced by a timer between events), durationMs,
│   │                      error, queueLength, repeat, eq, track (the whole Item), thumb;
│   │                      `fmt(ms)`
│   ├── Thumbs.h/.cpp      covers: `thumbs.file(url, size)` gives "file://…" or "" and asks the
│   │                      engine (one `thumbs` command per turn of the event loop);
│   │                      `ready(url, size, file)` when one lands
│   └── UiLog.h            appends to data/bbmusic-ui.log
├── tests/
│   └── queue_test.js      `node app/tests/queue_test.js`: checks assets/queue.js
└── assets/
    ├── main.qml           TabbedPane with the tabs in the tab menu: Search (inline), Home,
    │                      Explore, Recent and Queue (each a Tab delegate, created when first
    │                      opened); application menu: Settings, Stop, About; the error toast
    │                      (also "Skipped a non-music part."); `retire(page)`
    ├── SearchPage.qml     field, suggestions, segmented filter, results by kind with covers
    ├── AlbumPage.qml      an album or a playlist (read page by page): header row, tracks,
    │                      Play / Shuffle
    ├── ShelfPage.qml      a page of shelves: an artist, or a feed (Home, Explore; More)
    ├── RecentPage.qml     the listening history
    ├── QueuePage.qml      the queue: covers, rearrange mode, Shuffle, Sort, Clear
    ├── queue.js           the Queue page's arithmetic (diff, move, the slot for Play Next)
    ├── HomeTab.qml, ExploreTab.qml, RecentTab.qml, QueueTab.qml
    │                      the NavigationPanes the delegates load
    ├── NowPlayingPage.qml the stock player's layout
    ├── TrackInfo.qml      the Properties sheet
    ├── EqSheet.qml        the equalizer's presets
    ├── SettingsSheet.qml  what to skip in a track (SponsorBlock), a switch per kind
    ├── MiniPlayer.qml     the strip: state mark, title, elapsed / total, progress line
    ├── ConnectionBar.qml  "the engine is not connected" + Retry
    └── images/            ic_*.png (81 px) and art_none*.png, drawn by scripts/gen-icons.py
```

**Loaded at start, so an error there is fatal:** `main.qml`, `SearchPage.qml`, `MiniPlayer.qml`,
`ConnectionBar.qml`. Everything else is created on demand (a Tab delegate or
`ComponentDefinition.createObject()`): a fault costs that page, not the app. Keep it that way
for new tabs and pages.

**The app checks its own QML.** While `data/check-qml` exists in the app's data folder (it does
now: `touch` it over ssh), every start compiles all the QML documents and writes
`check: ok X.qml` or `check: FAIL X.qml: <error>` to `bbmusic-ui.log`. It finds what the
compiler finds, not what a binding does when it runs. Read it after the user's first launch;
remove the file when the pages have been seen (it costs start-up time). It cannot be run from a
shell: `bb::cascades::Application` crashes there and `QmlDocument` aborts without one.

No file is over 450 lines; keep each under about 500, one page or class per file (the user's
rule).

## The design, from the user's screenshots

The user's brief (2026-11-14, with seven screenshots of the stock player and six of YTMusicWP on
a Windows Phone, copied to `debug/shots/`): "work on the search (with clearer separation between
albums and songs) and with the album art preview and informations about the tracks, the album
view, the queue (with possibility to rearrange and sort) and the player interface. The
miniplayer can also be reduced to a strip with just the title to reserve more space, and the
controls can stay in the player ui. for the player UI i would like to inspire from the stock
BB10 player, but with the moder functionality expressed in the WP interface", "trying to stay
as Cascades as possible".

Measured on the stock player (720 × 720, 9 px per du):

| Stock player | Measured | Ours |
|---|---|---|
| Now Playing: title, artist | y 0–90; white ~40 px, grey ~26 px | `FontSize.Large` white, `FontSize.Small` #969696, 0.5 du padding |
| hairline | y 91, #323232 | a 2 px Container |
| album row with Repeat and Shuffle at its right | y 92–178 | 9.5 du; two 8 du `ImageButton`s |
| artwork | y 178–612, placeholder a black note on #262626 | fills what is left, `AspectFill`; `art_none_large.png` |
| scrubber band over the artwork's lower edge | y 485–612, black at about 70 % | a Container #b0000000, the Slider, elapsed left and "-5:54" right |
| action bar | Back, Previous, Pause (Signature), Next, ⋮ | the same, as `ActionItem`s; the Signature colour is the theme's red |
| ⋮ menu | Play on, Previous, Pause, Next, Edit Order, Add to Favorites, Add to Playlist, View Album, Share, Set As Ringtone, Equalizer, Audio, Properties | Queue, View Album, View Artist, Start Radio, Share, Equalizer (Off/On), Properties (favourites and playlists need an account, Phase 4) |
| library: segmented control, rows with an 88 px cover at the left edge | | Search's filter is that control; rows are `StandardListItem`s with `imageSource` |
| library action bar: tab menu, Shuffle (Signature), ⋮ | | the Queue page: tab menu, Edit Order, Shuffle (Signature), ⋮ |
| tabs in the tab menu, not on the bar | | `showTabsOnActionBar: false` |

From YTMusicWP's screens, what each of ours must be able to do:

| YTMusicWP | Ours |
|---|---|
| Search: chips Album / Songs / Artists / Video / Playlist; a top-result card; rows with cover, "Song • artist • plays" | the segmented control (All, Songs, Albums, Artists; videos and playlists show under All); a "Top result" header; rows with cover, artist, album, plays, length |
| Player: cover, title, artist, queue button, like, scrubber, shuffle, previous, play, next, repeat | all but like (Phase 4); the queue is an action |
| Player's Up Next: count, Clear, per-row up / down / remove, the playing row green | the Queue page: count in the header, Clear, the system's rearrange mode for moving, Remove and Play Next on a long press, the playing row red |
| Mini-player: cover, title, artist, play, next, progress | by the user's wish a strip: title and progress only |
| Home | a tab: the home feed's shelves as headers and rows (no carousels, no chips), More for the next shelves |
| Library: Recently Played | a tab, Recent, from the engine's own history |
| Library's rest, Samples, Create | not built (Phase 4, the account) |

## What the user decided at their first look (2026-11-14, 0.1.0.19)

Their words: "shuffle should be a toggle, when sort is toggled it covers the time, should push
it to the lefy. primary color is ok, strip could be 20%taller. extra tabs are ok."

- **Shuffle is a mode**, a toggle, as in the stock player: on shuffles what is still to come
  and remembers the order; off puts it back. (It was a one-shot action in 0.1.0.9–0.1.0.19.)
- **In Edit Order the grip must not cover the time**: in that mode a row's time leads the line
  under its title and the right end is left to the grip.
- **The primary colour stays YouTube's red.**
- **The strip is a fifth taller**: 7.2 du.
- **Home, Explore and Recent stay.**

Then, from a screenshot of 0.1.0.20's queue in Edit Order (`debug/shots/IMG_20261114_203425.png`)
and three more remarks:

- "on Float on now the album overlays the queue handles": in Edit Order both lines of a row are
  cut, with an ellipsis, to end before the grip. The cut is an estimate of the text's width
  from that screenshot's own measurements (`queue.js`: `textUnits`, `fit`; a unit is 19.3 px in
  a title, 15.5 px under it, on the Q10), because a `StandardListItem` cannot be told where to
  stop. If it misses on another screen or font size, the row becomes a custom item with real
  padding.
- "a long title in the strip gets behind the cascades button, but i can be fine with that. a
  time elapsed / time total counter could be at the other end": the strip ends with
  "1:23 / 3:22" in grey. The title still runs behind a page's Signature button.
- "on tab change the spinner is at the top instead of in the center of the screen": a loading
  page's spinner sits in the middle of the list's space (ShelfPage, AlbumPage).
- "the stop button should not be present in the quick menu in the player, why is it
  necessary?": it is not; gone from Now Playing's menu. Stop is still in the application menu
  (swipe down), where it has been since 0.1.0.4; not asked about.

Still theirs to confirm, not mentioned yet:

- **Sort sorts the whole queue**, played tracks included; the current track stays current.
- **The strip shows a small play/pause mark** beside the title (what the player is doing, not a
  button) and a red progress line.
- **Videos and Playlists have no segment** in Search (the control holds four): they appear under
  All.
- **Start Radio from Now Playing restarts the track** (it is `play` with `radio`).
- **Repeat One**: Next still moves on; only a track ending by itself repeats.
- **The tab order**: Search first, then Home, Explore, Recent, Queue.
- **The equalizer** is ten fixed presets, no sliders; their gains are a first guess.
- **SponsorBlock is in, and off.** Turning a switch on makes the engine ask sponsor.ajay.app
  about every track that starts, by the first four hex digits of the SHA-256 of its id (the
  service then answers for every video sharing them). That is a third party learning roughly
  what is played: the user's call. A slow answer holds the engine's worker for up to 10 s.

## Build, install, look

```sh
scripts/build-app.sh                      # engine (Docker, bbmusic-build) + UI (Docker, yamsergey/bb10-ndk:0.6)
SKIP_ENGINE=1 scripts/build-app.sh        # UI only, reusing build/engine-bb10/bbmusicd
scripts/install-phone.sh build/bbmusic.bar   # the Q10 over root ssh (bb10rootx); prints result::success
```

- Each build gets the next build id; check `Package-Version` in the script's output against
  what you tell the user.
- `scripts/build-app.sh` runs `scripts/check-qml-handlers.py` first: every `onSomething:` in
  the QML must be a signal of the element it sits in (the lint does not look at handler names,
  and a wrong one stops the app at start), and `node app/tests/queue_test.js`.
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

## What works on the phone, and what has not been tried

**The user opened 0.1.0.19 on 2026-11-14 at 20:56** and looked for under a minute: all 16
documents compiled on the phone (the `check:` lines), the restored queue came back paused, Home,
Explore and Queue opened, a drag in Edit Order reached the engine as `queue_move`, 140 covers
were fetched, and neither log has an error. What they said is under "What the user decided".
They then looked at 0.1.0.20 (a screenshot of the queue) and sent four more remarks; **0.1.0.22,
with all of it, is installed and not yet reported on.** The search, the album and
artist pages, Now Playing, Recent, the equalizer, Settings and SponsorBlock have not been
reported on (nothing was played in that session). The engine's additions ran live on the Mac
(and the first of them on the phone from a shell), the equalizer only in its tests. Still the
things to look at, in this order, with `bbmusic-ui.log` open (a `QML:` line there means the app
quit at start; the `check:` lines say which documents compile):

1. The app starts; the tab menu shows Search and Queue; the theme is red.
2. Search: the segmented control; covers arrive in the rows; headers per kind.
3. A tap on an album opens its page; the header row; Play, Shuffle.
4. The strip appears; a tap opens Now Playing; the cover; Repeat and Shuffle icons; the bar.
5. Queue: covers; the playing row in red (it is HTML in a `StandardListItem`: if the row shows
   markup, drop it for a plain title); Edit Order and a drag; Sort; Clear.
6. Properties; Share; View Album and View Artist from the player.
7. The tab menu's Home, Explore, Recent and Queue: each is created when first chosen. A tab
   whose file fails to load shows a toast with the error and stays on the page before.
8. An artist from Search: the header, Top songs, the shelves; Play.
9. A playlist of more than 100 tracks (it fills in page by page); the equalizer from Now
   Playing's menu, by ear.
10. Settings in the application menu (swipe down): the four switches; with "Non-music parts"
    on, a music video with a talking intro (kJQP7kiw5Fk starts 21.8 s in) and the toast.

By the user's report on 0.1.0.7 ("works, plays stops scrubs") and its logs: the engine starts
with the app and connects; suggestions and search; a tap plays with a radio queue of 50; Now
Playing opens without disturbing playback; pause, resume and the scrubber; the queue restored
paused after a restart; playback from the cache in under 0.2 s.

Never tried by the user: context actions; Next and Previous; one song running into the next;
the keyboard shortcuts (Space, N, P; now also Q, A, I on the player, E and S on the queue, S on
an album); the application menu; the volume overlay's buttons with the engine holding
now-playing; swiping the app away mid-song and coming back; an error toast with Wi-Fi off.

Open questions from testing: "first play request did not pass, second worked" on 0.1.0.4,
unexplained and not seen since; an underrun line in the engine's log after some resumes and
seeks, not known to be audible.

## What the UI needs next, in the plan's order

The steps are PLAN.md §9; what each needs from the engine is noted, because several need a
command that does not exist yet. A new command goes into `docs/protocol.md` first, then the
engine (`engine/src/engine/commands.cpp`), then the UI.

| Step | UI | Engine side |
|---|---|---|
| 2.5 Artwork | **built, unseen:** rows and the player show covers through `Thumbs` | there (`thumbs`, the `thumb` event) |
| 2.6 Now Playing | **built, unseen:** the stock layout, icons, Properties | there (`repeat`, `queue_shuffle`, `Item.plays`, `Item.year`) |
| 2.8 Queue | **built, unseen:** rearrange mode, Shuffle, Sort, Clear | there (`queue_move`, `queue_shuffle`, `queue_sort`, `queue_clear`) |
| 2.9 Overlay | nothing in the UI | there; artwork in the overlay untried (`albumArtwork`, a `file://` path) |
| 2.10 Active Frame | a cover with title and state, updated only while thumbnailed | none |
| 2.11 Resume and errors | what the pages show while loading, with no network, for an unplayable track | events are there; check the wording of `error` messages |
| 2.12 Measure | — | memory, CPU, battery per hour, tap-to-sound |
| 3.1 Home | **built, unseen:** a tab of shelves (`ShelfPage`), More | there (`browse` of `FEmusic_home`, `browse_more`); the mood chips are not read |
| 3.2 Album, playlist | **built, unseen:** one page for both; a long playlist page by page | there (`browse`, `browse_more`) |
| 3.3 Artist | **built, unseen:** `ShelfPage`; opens from Search, an album, Now Playing | there (`browse` of `UC…`); "more" of a shelf (all albums, all songs) is missing |
| 3.4 Explore | **built, unseen:** a tab on `FEmusic_explore`; charts, moods and genres are not | `FEmusic_charts` and `FEmusic_new_releases` parse too; `FEmusic_moods_and_genres` comes back empty (another renderer) |
| 3.5 Item actions | play next, add to queue, start radio, go to artist/album, share | mostly there; "go to" needs 3.2 and 3.3 |
| 3.6 History | **built, unseen:** the Recent tab; opening links from other apps (an invoke target) is not | there (`history`) |
| 3.7 Region and language | a setting | `InnerTube::set_language` exists; **missing:** a command and a stored setting |
| 3.8 SponsorBlock | **built, unseen:** Settings, a switch per kind, off by default | there (`skip`, the `skipped` event); ran live on the Mac |
| 3.9 Equalizer | **built, unseen, unheard:** a sheet of presets from Now Playing | there (`eq`); presets only, no bands to set |

The renderers the missing `browse` pages return are parsed for search already
(`engine/src/innertube/items.cpp`); YTMusicWP's `InnerTubeClient.Browse.cs` is the map for the
rest.

## Not yours to change without asking

The protocol's framing, the engine's module contracts (`docs/plan/phase1.md`), and anything
under `engine/` beyond adding a command the UI needs. The user's delegation rules for
sub-agents are in their global instructions; for this project they allowed one Opus and two
Sonnet agents when working unattended, each in folders of its own, with the main session
reviewing and committing every step.
