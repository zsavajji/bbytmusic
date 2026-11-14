# BBMusic: where things stand

For whoever picks this up next. Brought up to date on 2026-11-14 at the user's request ("save
the plan for another agent to pickup the ui"). Start here, then follow the links.

## In one paragraph

A first working build is on the user's Q10: **`bbmusic.bar` 0.1.0.7**. Its engine, `bbmusicd`,
searches YouTube Music, resolves a track through the `VISIONOS` client, fetches it by DASH
ranges (SABR built and tested as the fallback), decodes Opus and plays it through libasound,
keeps a radio queue going, caches what it played, restores the queue after a restart, and holds
the system's now-playing controls itself. Its UI is a functional shell. The user's verdict on
it: "works, plays stops scrubs". The look comes next, from screenshots the user will provide.

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

## Next

1. **The UI's look**, when the screenshots arrive: [ui.md](ui.md).
2. Phase 2's remaining steps: artwork (2.5, needs an engine command), the Active Frame (2.10),
   loading and error states (2.11), measurements (2.12).
3. Phase 3, browse: home, album, playlist, artist, explore. Each needs a `browse` command in
   the engine first. Then SponsorBlock and the equalizer.
4. Phase 4, the account: gate 0.7 (WebView sign-in) has to be run first, with the user.

## Open

- A phone call and Bluetooth during playback (gate 0.6): the user could not test them.
- The headset button with the app closed: untried (`audio_manager` has button events).
- "first play request did not pass, second worked" on 0.1.0.4: unexplained, not seen since.
- A stream url that expires during a long pause: the track is skipped, not resolved again.
- SABR through the cache, and SABR's redirect, error and attestation paths against the real
  service: only the fake server has exercised them.
- An underrun line after some resumes and seeks: not known to be audible.
- The engine's size on the phone is 4.1 MB unstripped; stripping and trimming wolfSSL are not
  done.
