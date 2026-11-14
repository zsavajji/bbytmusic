# BBMusic: where things stand

For whoever picks this up next. Brought up to date on 2026-11-14, in the evening, at the end of
an unattended session on the UI. Start here, then follow the links.

## In one paragraph

The last build the user has **seen** is `bbmusic.bar` 0.1.0.7 ("works, plays stops scrubs"): its
engine, `bbmusicd`, searches YouTube Music, resolves a track through the `VISIONOS` client,
fetches it by DASH ranges (SABR built and tested as the fallback), decodes Opus and plays it
through libasound, keeps a radio queue going, caches what it played, restores the queue after a
restart, and holds the system's now-playing controls itself. On top of that, built from the
user's screenshots while they were away and **installed but never opened**: the UI after the
stock BB10 player, 0.1.0.16 (covers everywhere, an album page, an artist page, Home, Explore
and Recent tabs, the queue with rearrange, shuffle and sort, the stock Now Playing layout, the
mini-player as a strip, an equalizer), and in the engine `browse` (album, playlist, artist,
feeds), `browse_more`, `history`, the thumbnail cache, `queue_clear`, `queue_shuffle`,
`queue_sort`, `repeat` and `eq`. [ui.md](ui.md) lists what to look at first and the decisions the
user should confirm.

## Where everything is

| What | Where |
|---|---|
| The plan, the decisions, the user's answers | [PLAN.md](../../PLAN.md) |
| The gates run before anything was built, on the Mac and the phone | [phase0.md](phase0.md) |
| The engine: rules, contracts, steps, results, the builds tried on the phone | [phase1.md](phase1.md) |
| **The UI handover** | [ui.md](ui.md) |
| The UI ↔ engine protocol | [../protocol.md](../protocol.md) |
| The engine | `engine/` (`scripts/test-engine.sh`, `scripts/build-engine.sh`, `scripts/fuzz-engine.sh`) |
| The UI | `app/` (`scripts/build-app.sh`, `scripts/install-phone.sh`) |
| The Phase 0 probes and the gate app | `tools/gates/`, `gateapp/` (still installed on the phone as "BBMusic Gate"; it can go) |

## The repository

Everything is committed on `master` (the user: "stay on master"); there is no remote and
nothing is pushed. One commit per step, with what was run and what was not in the message.

## Done

- Phase 0, the gates: 0.1–0.5 passed; 0.6 passed for headphones only; 0.7 not started.
- Phase 1, the engine: every step, 1.1–1.14.
- Phase 2, early: the engine side of the speaker (2.2) and now-playing (2.9), packaging (2.1),
  and a first UI (2.3, 2.4, 2.6–2.8).
- Built and tested in the engine, built and **unseen** in the UI (2026-11-14, evening): artwork
  (2.5), the Now Playing redesign (2.6), the strip (2.7), the queue's reorder, shuffle and sort
  (2.8), Home (3.1), the album and playlist page (3.2), the artist page (3.3), most of the item
  actions (3.5: play next, add to queue, start radio, view album, view artist, share), the
  history page (3.6, as the Recent tab), Explore in part (3.4), the equalizer with presets (3.9).

## Next

1. **The user's first look at the new UI**, and what it shows: [ui.md](ui.md), "What works on
   the phone". Expect a round of fixes; nothing in it has been seen.
2. Phase 2's remaining steps: artwork in the system's overlay (2.9, `albumArtwork`), the Active
   Frame (2.10), loading and error states (2.11), measurements (2.12).
3. Phase 3's rest: charts and moods (3.4), opening links from other apps (3.6), region and
   language (3.7), "more" of an artist's shelf, SponsorBlock (3.8). Each needs the engine first.
4. Phase 4, the account: gate 0.7 (WebView sign-in) has to be run first, with the user.

## Open

- A phone call and Bluetooth during playback (gate 0.6): the user could not test them.
- The headset button with the app closed: untried (`audio_manager` has button events).
- "first play request did not pass, second worked" on 0.1.0.4: unexplained, not seen since.
- A stream url that expires during a long pause: the track is skipped, not resolved again.
- SABR through the cache, and SABR's redirect, error and attestation paths against the real
  service: only the fake server has exercised them.
- An underrun line after some resumes and seeks: not known to be audible.
- The engine's size on the phone is 4.2 MB unstripped; stripping and trimming wolfSSL are not
  done.
- Repeat against real streams, and a seek past a track's end under Repeat One (it moves on, as
  a manual Next does): only the tone files have exercised repeat.
- A cover that fails to download is not asked for again until the UI reconnects.
- The thumbnail cache is capped by count (3000 files, the 1000 oldest dropped at start), not by
  bytes.
