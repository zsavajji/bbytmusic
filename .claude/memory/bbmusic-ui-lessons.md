---
name: bbmusic-ui-lessons
description: What the Q10 taught on 2026-11-21 about Cascades transitions, and how the user tests UI changes with me
metadata:
  type: feedback
---

On 2026-11-20/21 the user tested Phase 4 builds on the Q10 live, one short remark per build ("signin button is too narrow", "still from right", "ok, better!"), and I rebuilt and installed after each (builds 1.0.1.55 to .67). What held, see also [[bbmusic-plan-status]]:

- **Read `../hyperberry/.claude/skills/bb10-ux` before proposing any transition.** I told the user a Sheet would slide Now Playing up from the bottom; on BB10 sheets slide in from the right, and that skill says so. It cost them two builds.
- The user wanted Now Playing to rise from the strip. What works: the player page takes `Application.scene`, its content plays a translate + fade, the page under it fades and hides its action bar first (`MiniPlayer.qml`, `NowPlayingPage.present()/leave()`). `setScene()` deletes a replaced scene the Application owns, so the root is parented in `main.cpp` and the player is created with `createObject(Application.scene)`. Give the scene back from an animation's `onEnded`, never from inside an action's handler (that closed the app).
- A container is only as wide as its content: a `Button` with `HorizontalAlignment.Fill` stays narrow unless its parents fill too.
- `TitleBar` needs `scrollBehavior: Sticky` on a page with a strip at the bottom.
- The phone's own `grep` is unreliable with `\|`: copy the log off (`ssh … cat`) and grep on the Mac.
- The Classic's Back belt key, on a page that is the scene (no NavigationPane to pop): `DeviceShortcut { type: DeviceShortcuts.BackTap }` (Cascades 1.4) gets it; `SystemShortcuts.PreviousSection`, the same key code, never fires and the app goes to the background (2026-11-28, 1.0.3.115/.116).
- `git add app/assets` sweeps in the user's uncommitted version bump (`AboutSheet.qml`): add files by name.

**Why:** nobody but the user can see the screen; each wrong guess is a round trip for them.
**How to apply:** check the platform notes first, say what is untested, install only when it will not interrupt what they are doing on the phone (an install restarts the app and stops the music).
