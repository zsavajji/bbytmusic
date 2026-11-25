# BBMusic: notes for agents

A YouTube Music client for BlackBerry 10: `engine/` is the headless C++20 `bbmusicd`, `app/` the Cascades UI
(Qt 4.8, C++03, QML `bb.cascades 1.4`); they talk NDJSON over loopback (`docs/protocol.md`).

Start with `docs/plan/status.md`, `docs/plan/ui.md` and `docs/plan/phase4.md`; the git log records what came
after them. Build: `scripts/build-app.sh` (`SKIP_ENGINE=1` to keep the engine). Engine tests:
`scripts/test-engine.sh`.

The files below are the memory of earlier sessions: what was built, what the user decided and how they want the
work done. Where two dated notes differ, the later one holds. When a session learns something that will matter
to the next one, add it there.

@.claude/memory/bbmusic-plan-status.md
@.claude/memory/bbmusic-ui-lessons.md
@.claude/memory/confirm-before-building-ambiguous.md

This repository is public: no client id, secret, token or unsanitised library response goes into it, and
`debug/` stays out of git.
