# BBMusic and Tube's sign-in card

BBMusic can use the YouTube account Tube is signed in with, so the user does not sign in twice. This is Tube's
card (`SIGNIN-CARD.md`, Tube 1.17.0) seen from the app that asks: what BBMusic sends, what the user sees here,
and what BBMusic does with the answer. Here BBMusic asks and Tube gives; the other way round - Tube asking
BBMusic - is BBMusic's own card, `signin-card.md`. BBMusic's own sign-in (a code entered at google.com/device)
stays next to it. Since BBMusic 1.0.2.

| | |
|---|---|
| Target | `doctorrokter.BBTube.card.signin`, invoked by target from C++ (`InvokeManager::invoke`) |
| Action | `doctorrokter.BBTube.SHARE_SIGN_IN` |
| Request | `application/json`, UTF-8: `{ "version": 1, "app": "BBMusic" }` |
| Answer | `bb::system::InvokeManager::childCardDone(const CardDoneMessage&)` |
| Asked by | `com.zsavajji.bbmusic`: the install id Tube's card shows begins with it |

The caller is the class of Tube's document, `app/src/TubeSignIn.cpp`, with one difference: it has no
`signedIn` signal. The answer goes from `childCardDone` straight to BBMusic's engine, in C++; the QML never
holds the token and the UI's log prints the outcome only.

## What the user sees

In BBMusic's Settings, under *YouTube account*, while signed out: **Sign in with Tube** above **Sign in**. The
first is there only when Tube's card is on the phone. A tap opens Tube's card; on **Allow** the card closes,
BBMusic checks the token with Google and asks the account's name (a second or two, both buttons greyed), and
Settings then shows the name, *Shared by the Tube app* and **Sign out**. On **Cancel** nothing changes and
nothing is said. When nothing could be given, one line says why: *The sign-in did not complete: Tube is not
signed in to YouTube*.

## 1. Is Tube installed

Each time Settings opens, BBMusic asks the system for the card (`queryTargets`: the action, `application/json`,
cards) and shows the entry when the target `doctorrokter.BBTube.card.signin` is among the answers. A Tube older
than 1.17.0, or no Tube: no entry.

## 2. Ask

```json
{ "version": 1, "app": "BBMusic" }
```

| Field | |
|---|---|
| `version` | `1` |
| `app` | `BBMusic`, the same in every language |

One card at a time: while a card is up, or its answer is being checked, a second tap does nothing.

## 3. The answer

| `reason()` | `data()` | What BBMusic does |
|---|---|---|
| `done`, with a `refreshToken` | `version`, `refreshToken`, `clientId`, `clientSecret`, `scope`, `tokenUrl` | hands `refreshToken`, `clientId`, `clientSecret` to its engine; see below |
| `cancel` | `{ "version": 1 }` | nothing: the user said no |
| `cancel` | `"error": "not-signed-in"` | *Tube is not signed in to YouTube* |
| `cancel` | `"error": "bad-request"` | shows the word as it came (it should not happen) |
| anything else, or none | - | nothing: the card was closed |
| the invoke fails | - | *Tube's sign-in did not open* |

**What BBMusic does with `done`.** The engine refreshes the token once, to prove it and the client pair, then
asks YouTube for the account's name. Only when the refresh answers does the account change: a failed one
leaves BBMusic as it was, with *Google does not accept Tube's sign-in* or *YouTube could not be reached*. The
refresh token and the client pair are then kept in the engine's own store, in BBMusic's sandbox; access tokens
are kept in memory only.

**What BBMusic reads, and what it does not.** `refreshToken`, `clientId`, `clientSecret` - the last two only
when both are there; without them BBMusic uses the "YouTube on TV" client it reads from `youtube.com/tv`.
`version`, `scope` and `tokenUrl` are not read, and fields a later Tube adds are ignored. BBMusic refreshes at
`https://www.youtube.com/o/oauth2/token` with a JSON body, as its own sign-in does, not at `tokenUrl`; both
endpoints took the same token when tried with one of BBMusic's own (2026-11-21).

## Things to know

- **Signing out in BBMusic ends both.** BBMusic revokes the token at Google when the user signs out here, and
  Tube is signed out with it: Tube's next refresh answers `invalid_grant`. This is not what Tube does on its
  side - signing out in Tube forgets its copy and BBMusic stays signed in.
- **BBMusic does not follow Tube.** It keeps the account it took until the user signs out here or takes another
  one; an account switch in Tube changes nothing in BBMusic.
- **A refused refresh** (`invalid_grant`: the access was taken back in the Google account): BBMusic signs itself
  out and the two entries are back in Settings.
- **The scope.** Tube's token has `https://www.googleapis.com/auth/youtube`; BBMusic's own sign-in also asks for
  `youtube-paid-content`. Whether the library, the playlists and the likes all answer without the second is not
  known yet.
- **The logs.** BBMusic's UI log has `tube: asked`, the answer's `reason` and `error`, and `account import`;
  the engine's log has the command's name. Neither has a token, a client id or a secret.
- **State, 2026-11-25:** tried on a phone and working.
