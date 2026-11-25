---
name: confirm-before-building-ambiguous
description: In BBMusic, when a request could mean a document or a code feature, ask before building the feature
metadata:
  type: feedback
---

On 2026-11-21 the user wrote "build the signin-card in the same format he did for his own" and meant rewriting the document `signin-card.md`; I built a whole feature (engine command, card class, QML, invoke target). They asked "so you changed code too?" and had it removed. Earlier the same day "return me a document mapping the inverse flow: from tube to here" meant BBMusic's side of the existing flow, not a new opposite-direction feature.

**Why:** their messages are short and the nouns here name both a file and a feature; a wrong guess toward code cost a sub-agent run, a revert and two rebuilds.

**How to apply:** when a short request can be read as "write/rewrite a document" or as "build a feature", and the feature reading means new code beyond a few lines, ask one question first (my interpretation: default to the document reading). Related: [[bbmusic-plan-status]].
