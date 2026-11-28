# BBMusic plan: docked mode

Written 2026-11-27, on the branch `feature/dock`.

## What the user decided (2026-11-27)

- Docked mode is for an external display, "hdmi or external display eventually": a cable or a
  wireless (Miracast) display alike.
- Option A of the two proposed: the phone keeps mirroring its screen (the system's default, as far
  as known) and the app switches to a layout made for the room. Not B (a separate picture on the
  TV drawn on a raw QNX Screen window, with the phone as a remote).
- "with also a manual toggle. large controls + background image in high res + eventually
  scrolling lyrics on the side or in the middle".

## Detection

`bb::device::DisplayInfo` for `DisplayInfo::secondaryDisplayId()`, made once in `main.cpp` and
given to QML as `externalDisplay` (QML can only make a `DisplayInfo` for the primary display).
`attached` and `attachedChanged(bool)` cover a cable and Miracast; `wireless` tells them apart,
unused for now. `-lbbdevice` in `bbmusic.pro`.

- Attached while a track is loaded: docked mode opens by itself.
- Detached: it closes, if it opened by itself. Opened by hand, it stays until closed.
- Attached with nothing loaded, or at the app's start: nothing (the manual toggle covers it).

## The manual toggle

"Docked Mode" in Now Playing's drawer (shortcut `D`). It opens over Now Playing, and closing
it brings Now Playing back. Now Playing reaches the root through its `backScene` (the TabbedPane),
which owns the page's `ComponentDefinition`. A page made from a definition on Now Playing would
lose its QML context when Now Playing is destroyed (1.0.1.75, see `MiniPlayer.qml`).

## The page (`app/assets/DockPage.qml`)

Shown as Now Playing is: it takes `Application.scene` and gives the previous one back when it
leaves (`present()` / `leave()`), with a fade and no action bar.

- **Background**: the artwork across the whole screen, `AspectFill`, asked from `thumbs` at the
  screen's own size in pixels (from the page's `LayoutUpdateHandler`, capped at 1440: the
  Passport). A dark layer over it (`#99000000`) keeps the text readable. A video's 16:9 frame comes
  at whatever size YouTube has.
- **Top**: title (XX-large, white, bold), artist and album (large, light grey), no Close (since
  1.0.3.91: "Exit Docked Mode" is in the drawer).
- **Middle**: empty (the artwork shows through), or the lyrics when they are on. `LyricsView` as
  in Now Playing: autosync, Sync, tap to seek. The lyrics go in the middle: on the square screens
  (720 and 1440 px) half a width is too narrow for large lines. Lyrics "on the side" is left for a
  landscape screen, if the portrait lock is ever lifted.
- **Bottom**: no scrubber (the user, 2026-11-27: "no scrubber is needed in docked mode"), only
  the time as text ("1:23 / 3:45"), then one row of large buttons: Lyrics (8 du), Previous,
  Play/Pause, Next (14 du each, Play/Pause 18 du), Favorite (8 du). The icons are the existing
  ones.
- **Screen kept awake** while the page is up: `Application.mainWindow.screenIdleMode =
  ScreenIdleMode.KeepAwake`, back to `Normal` when it leaves.

The lyrics request code is copied from Now Playing (about 40 lines), not shared. Moving it into
`LyricsView` would change a view the user has approved; that can wait until a third user comes.

## Steps

1. `main.cpp`, `bbmusic.pro`: `externalDisplay`.
2. `DockPage.qml`; the definition and `dock(auto)` / auto-close in `main.qml`; the action in
   `NowPlayingPage.qml`; strings in the 8 translations.
3. Lint, one build, installed when the user is at the phone.

## Untested

- That BB10 mirrors the app to the external display on these phones (Q10, Q20, Passport: none
  has a wired port as far as known, so Miracast).
- `attachedChanged` for a Miracast display on 10.3.3.
- A 1440 px JPEG background on the Q10's memory.

## On the phone (2026-11-27)

1.0.3.90 on the Passport, opened by hand. The user: "docked mode lyrics should be bigger, and the
background art is not showing"; "change the way to exit that with an action into the drawer on
top"; "move View Artist / View Album / Start Radio / docked mode inside there when in now playing".
Their screenshot showed the empty-note picture: the 1440 px cover (1080 px at Google) was still
downloading. So, for the next build:

- The art: Now Playing's 544 px cover stands in until the large one comes; the lyrics' own dark
  layer is lighter here (`#55000000`) so the art shows behind them too.
- The lyrics two sizes up (`LyricsView.big`): the lit line XX-large, the others X-large.
- No Close on the page: "Exit Docked Mode" in the drawer. Each scene has its own drawer (the tabs'
  one is empty while Now Playing is up), so Now Playing has its own too: View Album, View Artist,
  Start Radio, Docked Mode, out of the action menu. The A and D letters stay, on the page.

Later the same day, builds .92 to .107, each from one remark of the user's on the Passport: the
drawer fills its corners first (first action left, second right, the rest between), so the code
lists Radio, Docked Mode, Album, Artist to show Radio, Album, Artist, Docked Mode; the Docked Mode
icon is a TV (`ic_tv`); the docked buttons are drawn at full size (`*_big.png`, 432 and 180 px), the
81 px ones stretched were soft; Previous / Play / Next 20 / 24 / 20 du with Lyrics and Favorite in a
column at the top right; the lyrics as the user's example (one colour from the cover over the
whole page, `Thumbs::tint`; one size, XX-large, lines close together; sung faded, current white,
coming dark), synced only ("either synchro mode or nothing": no Sync button, plain lyrics not
shown), one sung line above the current one, 3 du apart from the artist and the time; the time
X-large; Play/Pause fades into a spinner while loading. Outside docked mode: the strip twice as tall
on the Passport, 1.2 times elsewhere, its text 110 % / 100 % (percentages of Medium: 200 and 135
were too big), its caret 5 / 3.6 du; Now Playing's Close 6 du; a track restored without a length
(Home's items have none) gets it saved by the engine once it plays, and the UI no longer shows it
at its end meanwhile. The user: "works fine" (1.0.3.107).
