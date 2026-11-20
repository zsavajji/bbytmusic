# BBMusic plan: Phase 4, the account

Written 2026-11-20. It replaces `PLAN.md` §6 ("cookie login only") and its steps 4.1 and 4.2:
gate 0.7b ([phase0.md](phase0.md)) showed that a TV sign-in opens the library, and the user wants
that flow ("i know for a fact that the tv flow works in the Tube app, so i would likely mirror
it").

## What the user decided (2026-11-20)

- A sign-in made in the **Tube** app is shared with BBMusic: "to share an eventual login made in
  the Tube app". This comes first.
- BBMusic also gets its **own** sign-in, later: "i would also want the login to work here
  standalone eventually".
- BBMusic refreshes the token itself ("so we can also refresh the token eventually").
- Tube is closed source: "we can have no code from Tube, just coordinate with their dev". What
  Tube does is an agreement with its developer, not something to read.

## The sign-in, as SmartTube does it

Read in `yuliskov/MediaServiceCore` (SmartTube's YouTube library, commit `c1d202f`, 2026-11-19;
`youtubeapi/.../auth/V2/` and `app/models/ClientData.java`). It is what gate 0.7b did by hand.

- The OAuth client is YouTube TV's own. Its id and secret are **read at run time**: load
  `https://www.youtube.com/tv`, take the `base-js` script's url, and in that script match
  `clientId:"….apps.googleusercontent.com",<name>:"<secret>"`. The first match is the one that
  works; SmartTube notes that older ones answer 401. Nothing of the pair is kept in this
  repository, which is public.
- The code: `POST https://www.youtube.com/o/oauth2/device/code`, JSON `client_id`, `device_id` (a
  uuid), `model_name` `ytlr::`, `scope` `http://gdata.youtube.com
  https://www.googleapis.com/auth/youtube-paid-content`. The user enters it at
  `https://www.google.com/device`.
- The token: `POST https://www.youtube.com/o/oauth2/token`, JSON `code` (the device code),
  `client_id`, `client_secret`, `grant_type` `http://oauth.net/grant_type/device/1.0`, at the
  interval the code's answer gives, until it stops answering `authorization_pending`.
- The refresh: the same url, JSON `refresh_token`, `client_id`, `client_secret`, `grant_type`
  `refresh_token`. A refresh token only works with the pair that issued it, so the engine stores
  the pair next to the token.
- The account's name and photo: `youtubei/v1/account/accounts_list` with the `TVHTML5` client
  and `Authorization: Bearer`.

## What a TV token gives (gates 0.7b and 0.7c)

The library, Liked Music and the account's playlists, through the `TVHTML5` client, in TV
renderers (a second parser). No personalised Home: only `WEB_REMIX` serves it, and it refuses
the token. Gate 0.7c's table is below.

## Sharing Tube's sign-in: the contract proposed to its developer

His idea: BBMusic invokes Tube, Tube asks the user in a native alert, and on Yes hands the token
back through the invocation framework. Proposed shape (checked against the 10.3.1 headers:
`InvokeRequest::source()`, `InvokeManager::sendCardDone()`, the `childCardDone()` signal):

- **Request.** Tube exposes a card invoke target with an action of its own (he names both).
  BBMusic invokes it by target id, with no data. Not a `tube://` uri: any app can claim a scheme.
- **In Tube.** The alert names the caller from `InvokeRequest::source()` (`installId`,
  `groupId`), not from anything BBMusic sends.
- **Response.** `sendCardDone()`, `dataType` `application/json`, and a `reason`:
  `shared` with `{"v":1,"refresh_token":"…","access_token":"…","expires_in":…,"client_id":"…",
  "client_secret":"…"}`; `denied` when the user says No; `signed_out` when Tube has no account.
  Only the app that opened the card gets `childCardDone()`, so no other app can receive the token
  and BBMusic needs no invoke target of its own.
- **Sign-out.** BBMusic only forgets a shared token and never revokes it, so Tube stays signed
  in. Revoking the grant anywhere (Tube, the Google account page) signs out both.

Open with him: whether his sign-in is this flow; whether he shares the refresh token and the
client pair. Without the pair the engine uses the one it reads from `youtube.com/tv`, which is
his too if his flow is this one. With only the access token, sharing lasts a day and a headless
player cannot ask again: the standalone sign-in then moves up to replace it.

## Steps

| Step | What | State |
|---|---|---|
| 4.0 | Gate 0.7c: `tools/gates/tv_auth.py` on the Mac, the user enters one code. What the token can do, and whether two refreshes can live side by side (Tube and BBMusic) | done 2026-11-20, see below |
| 4.1 | Engine: the account. The refresh token, the client pair, the name and the photo in `settings`; refresh when the access token has under a minute left; a refused refresh signs out. Protocol: `account` (bare: report; `import`; `sign_out`) and an `account` event | built 2026-11-20 (`engine/src/engine/oauth.*`, `account.*`, `account_cmd.cpp`); its tests pass, and on the Mac the real service took an `import` of the gate's token with and without the client pair, kept it across a restart and refused a made-up token. The engine cross-builds for BB10 with it; not yet run on the phone |
| 4.2 | UI: an Account row in Settings; "Sign in with Tube" opens Tube's card and passes its answer to `account` `import`. Shown only when Tube's target is installed. Waits for Tube's side | |
| 4.3 | Library: the TV renderers parsed into Items; a Library tab. Planned from 4.0's saved responses | the parser is built and tested on fixtures (`engine/src/innertube/tv_browse.*`, 2026-11-20). Not built: the engine's requests with the token, the command, the UI |
| 4.4 | The standalone sign-in: `sign_in` and `cancel` on `account` (the code request and the polling), a sheet with the code and the url. Sign-out revokes a token of our own | |
| 4.5 | Like, unlike; playlists: create, add, remove, delete (if 4.0 says the TV client does them) | |

One account at a time. The engine records where it came from (`tube` or `own`) and Settings
shows it. BBMusic does not follow an account switch made in Tube: it keeps what it imported
until the user signs out or imports again.

The engine logs a command's name only (`server.cpp`), never its fields, so a token is not
logged there; the UI's log must not print the card's answer.

## Gate 0.7c

`python3 tools/gates/tv_auth.py code`, then `python3 tools/gates/tv_auth.py --writes`. The token
and the raw responses go to `debug/tv/`, which git ignores: the responses are the account's
library, and fixtures made from them must be rewritten by hand first.

Run on 2026-11-20 with the user's account (they entered the code). The token answered with
`expires_in` 80598 (about 22 h), a refresh token, and the scope `youtube` plus
`youtube-paid-content`.

| Check | Answer |
|---|---|
| Refresh, twice in a row | 200 both; **no new refresh token**, two different access tokens |
| `accounts_list` with the first access token, then with the second | 200 both: the first still works after the second refresh, so Tube and BBMusic can refresh side by side |
| `accounts_list`'s shape | `contents[0].accountSectionListRenderer.contents[0].accountItemSectionRenderer.contents[]`, each an `accountItem`: `accountName.simpleText`, `accountPhoto.thumbnails[].url` (yt3.ggpht.com, 216 px), `isSelected`, `accountByline.simpleText` on the owner, `channelHandle`. Two came back (the owner and a brand account); `isSelected` marks the one signed in |
| `browse` `FEmusic_liked_playlists`, music.youtube.com | 200, `tvBrowseRenderer` |
| `browse` `VLLM`, music.youtube.com and www.youtube.com | 200, `tvBrowseRenderer` |
| `browse` `FEplaylist_aggregation`, `FElibrary`, www.youtube.com | 200, `tvBrowseRenderer`, `tileRenderer`s (4 and 43) |
| `browse` `FEmusic_home`, music.youtube.com | 400, as in 0.7b |
| `player`, `TVHTML5` with the token | UNPLAYABLE, "The page needs to be reloaded.", as without a token (gate 0.1): the token does not change playback, which stays on `VISIONOS` |
| `like/like`, `like/removelike`, www.youtube.com | 200 both |
| `playlist/create` with `privacyStatus` `PRIVATE` and empty `videoIds` | 400 `FAILED_PRECONDITION`; nothing was created. SmartTube sends `title` alone (and `videoIds` when it has one). Not retried: what privacy such a playlist gets is unknown, so it waits for 4.5 and the user's word |

Not run: `--revoke` (the token in `debug/tv/` is kept to test 4.1's `import`).

## 4.3: the library as the TV client gives it (probed 2026-11-20)

All with `TVHTML5` and the token, on music.youtube.com. Read-only requests.

- **The library is five tabs**, each a browse id: `FEmusic_last_played` ("Recent activity"),
  `FEmusic_liked_playlists` ("Playlists"), `FEmusic_liked_albums` ("Albums"),
  `FEmusic_liked_videos` ("Liked songs"), `FEmusic_library_corpus_artists` ("Subscriptions").
  A `browse` of any of them answers the whole page, `tvBrowseRenderer` →
  `tvSecondaryNavRenderer` → `tabRenderer`s, with that tab `selected` and its content filled;
  the other tabs hold only a reload continuation. So a tab is asked for by its own id.
- **Everything is a `tileRenderer`**: `contentType` (`TILE_CONTENT_TYPE_VIDEO`, `_PLAYLIST`,
  `_CHANNEL`), a title, lines of text items (a "•" is an item of its own), a time overlay, and
  `onSelectCommand`: a `watchEndpoint` (with `musicVideoType`, which tells a song from a video
  from a podcast episode) or a `browseEndpoint` (`VL…` for a playlist). An empty tab is a
  `genericPromoRenderer`.
- **A playlist** (`VL…`, the same answer on music.youtube.com and www.youtube.com): a
  `twoColumnRenderer`, its name and byline in `entityMetadataRenderer`, its tracks 15 at a time
  in `playlistVideoListRenderer` with a `nextContinuationData`; the next page is
  `continuationContents.playlistVideoListContinuation`. This is what a private playlist needs:
  the anonymous `WEB_REMIX` browse the app uses today cannot see it.
- The user's account has one music playlist, no saved albums, no liked songs and no
  subscriptions, so an album tile and an artist tile have **not been seen**: the parser reads
  them from their ids' shape (`MPRE…`, `UC…`) until an account that has some is tried.
- Fixtures: `engine/tests/fixtures/innertube/tv/`, made from these answers with every text, id,
  url and token replaced and at most three tiles of a kind kept.

Still to decide for 4.3, with the user: what the Library tab shows and in what order; whether
"Recent activity" replaces or joins the app's own Recent page.
