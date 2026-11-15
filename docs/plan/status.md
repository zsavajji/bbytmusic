# BBMusic: where things stand

For whoever picks this up next. Brought up to date on 2026-11-14, in the evening, at the end of
an unattended session on the UI. Start here, then follow the links.

## In one paragraph

The user has **seen** 0.1.0.7 ("works, plays stops scrubs") and, for a minute on 2026-11-14 at
20:56, 0.1.0.19: the UI rebuilt after the stock BB10 player from their screenshots (covers
everywhere, an album page, an artist page, Home, Explore and Recent tabs, the queue with
rearrange, shuffle and sort, the stock Now Playing layout, the mini-player as a strip, an
equalizer, Settings for SponsorBlock). It started, every page compiled on the phone, and they
asked for three changes (shuffle as a toggle, the time clear of the grip in Edit Order, a
taller strip), which are in 0.1.0.25. On 2026-11-15 they found every page fine on
0.1.0.25/26 and answered the open questions; 0.1.0.27 (sort only what is to come, Start Radio
that lets the song play on) and 0.1.0.29 (the
Active Frame, 2.10; the cover in the overlay, 2.9) are installed and not yet seen. Under it the engine,
`bbmusicd`: search, `browse` (album, playlist, artist, feeds), `browse_more`, `history`, the
thumbnail cache, resolve through the `VISIONOS` client, DASH ranges (SABR as the fallback), Opus
through libasound, a radio queue, `queue_clear`, `queue_shuffle`, `queue_sort`, `repeat`,
`shuffle`, `eq`, `skip`, the cache, the queue restored after a restart, the system's now-playing
controls. [ui.md](ui.md) has what they decided, what is still theirs to confirm and what nobody
has looked at yet.

**The test phone is a Q20 (BlackBerry Classic)** since 2026-11-15 (the user: "it's Q20 now"):
720 × 720 like the Q10, so the layouts measured on the Q10 hold; it adds a trackpad and the
call, menu, back and end keys. `ssh bb10rootx` (USB) and `scripts/install-phone.sh` reach it.
Older notes say Q10.

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
  history page (3.6, as the Recent tab), Explore in part (3.4), SponsorBlock (3.8), the equalizer with presets (3.9).

## Next

1. Done 2026-11-15: the user's look at the new UI; every page fine (ui.md, "second look").
2. Phase 2: the overlay's cover and Next (2.9) fixed in 0.1.0.34, unseen; the Active Frame
   (2.10) unseen; 2.11 done; 2.12 measured but for the battery (ui.md, the step table).
3. Phase 3's rest: charts and moods (3.4), opening links from other apps (3.6), language (3.7;
   the item parser reads English labels; the country is done, 0.1.0.32),
   "more" of an artist's shelf. Each needs the engine first.
4. Phase 4, the account: gate 0.7 (WebView sign-in) has to be run first, with the user.

## Open

- A phone call and Bluetooth during playback (gate 0.6): the user could not test them.
- The headset button with the app closed: untried (`audio_manager` has button events).
- "first play request did not pass, second worked" on 0.1.0.4: unexplained, not seen since.
- A stream url that expires during a long pause: a cached track's stream is now resolved again
  once (tested with a fake source only); a track that is not cached is still skipped.
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
