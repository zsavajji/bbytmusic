# BBMusic's sign-in card

Tube can use the YouTube account BBMusic is signed in with, so the user does not sign in twice. Tube opens
BBMusic's card, the card asks the user whether this app - by the name it sends - may use the account, and the
answer comes back to the app: **Allow** - BBMusic's sign-in, **Cancel** - nothing. It is Tube's own card
(`SIGNIN-CARD.md`, Tube 1.17.0) the other way round: the same request, the same answer, field for field.
Since BBMusic 1.0.2. **For now the card answers Tube only.**

| | |
|---|---|
| Target | `com.zsavajji.bbmusic.card.signin`, a `card.picker` - a bound invoke (`setTarget`) |
| Action | `com.zsavajji.bbmusic.SHARE_SIGN_IN` |
| Request | `application/json`, UTF-8 |
| Answer | `bb::system::InvokeManager::childCardDone(const CardDoneMessage&)` |

**Call the card from C++** (`InvokeManager::invoke`), as Tube's document says of its own: QML's `Invocation`
cannot read a card's answer.

The code below is the class of Tube's document with the two ids changed, NDK only: `bb::system`
(`LIBS += -lbbsystem`) and `bb::data::JsonDataAccess` (`LIBS += -lbbdata`). It is one class, `BBMusicSignIn`:
`check()` - is the card there, `ask()` - open it, and the answer as a signal.

## What the user sees

The card's title bar: BBMusic, **Cancel** on the left, **Allow** on the right. Under it the question - *Allow
"<your app's name>" to use your YouTube account from BBMusic?* - the account's name, what the app will be able
to do, and *Asked by: <install id>*: your app's install id as the system gives it. The name is your app's own
word, the id is the system's, so the user sees who really asks. When BBMusic is not signed in, the request
cannot be read, or the caller is not Tube, the card says so in red and only Cancel answers. The card speaks the
system's language, of BBMusic's nine (English otherwise).

The account's name comes from BBMusic's engine, a process of its own: when it is not running yet, the card
shows a spinner for a few seconds before the question.

## 1. Is BBMusic installed

Show your "Sign in with BBMusic" entry only when the card is there - no entry that does nothing. Ask the system
once, on the UI thread.

```cpp
// BBMusicSignIn.hpp
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace bb {
    namespace system {
        class CardDoneMessage;
        class InvokeManager;
    }
}

class BBMusicSignIn : public QObject
{
    Q_OBJECT
    // BBMusic's card is on the phone: show the "Sign in with BBMusic" entry.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    explicit BBMusicSignIn(QObject* parent = 0);
    bool available() const { return m_available; }
    Q_INVOKABLE void check();
    Q_INVOKABLE void ask();

Q_SIGNALS:
    void availableChanged();
    // The user allowed: version, refreshToken, clientId, clientSecret, scope, tokenUrl.
    void signedIn(const QVariantMap& signIn);
    // Nothing came: "" - the user said no or the card was closed; "not-signed-in", "bad-request",
    // "not-allowed" - BBMusic's reason; "no-card" - the card did not come up.
    void refused(const QString& error);

private Q_SLOTS:
    void onQueryFinished();
    void onInvokeFinished();
    void onChildCardDone(const bb::system::CardDoneMessage& message);

private:
    bb::system::InvokeManager* m_invoke;
    bool m_available;
    bool m_waiting;                     // a card of ours is up: the next childCardDone is its answer
};
```

```cpp
// BBMusicSignIn.cpp
#include "BBMusicSignIn.hpp"

#include <bb/system/CardDoneMessage>
#include <bb/system/InvokeAction>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeQueryTargetsReply>
#include <bb/system/InvokeQueryTargetsRequest>
#include <bb/system/InvokeReplyError>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTarget>
#include <bb/system/InvokeTargetReply>
#include <bb/data/JsonDataAccess>

static const char* const kBBMusicTarget = "com.zsavajji.bbmusic.card.signin";
static const char* const kBBMusicAction = "com.zsavajji.bbmusic.SHARE_SIGN_IN";

BBMusicSignIn::BBMusicSignIn(QObject* parent) :
        QObject(parent),
        m_invoke(new bb::system::InvokeManager(this)),
        m_available(false),
        m_waiting(false)
{
    connect(m_invoke, SIGNAL(childCardDone(const bb::system::CardDoneMessage&)),
            this, SLOT(onChildCardDone(const bb::system::CardDoneMessage&)));
}

void BBMusicSignIn::check()
{
    bb::system::InvokeQueryTargetsRequest request;
    request.setAction(QString::fromLatin1(kBBMusicAction));
    request.setMimeType(QString::fromLatin1("application/json"));
    request.setTargetTypes(bb::system::InvokeTarget::Card);
    request.setActionType(bb::system::InvokeAction::All);
    bb::system::InvokeQueryTargetsReply* reply = m_invoke->queryTargets(request);
    if (reply == 0) {
        return;                         // not asked: no entry
    }
    connect(reply, SIGNAL(finished()), this, SLOT(onQueryFinished()));
}

void BBMusicSignIn::onQueryFinished()
{
    bb::system::InvokeQueryTargetsReply* reply = qobject_cast<bb::system::InvokeQueryTargetsReply*>(sender());
    reply->deleteLater();
    bool installed = false;
    if (reply->error() == bb::system::InvokeReplyError::None) {
        const QList<bb::system::InvokeAction> actions = reply->actions();
        for (int i = 0; i < actions.size(); i++) {
            const QList<bb::system::InvokeTarget> targets = actions.at(i).targets();
            for (int j = 0; j < targets.size(); j++) {
                if (targets.at(j).name() == QLatin1String(kBBMusicTarget)) {
                    installed = true;
                }
            }
        }
    }
    if (installed != m_available) {
        m_available = installed;
        emit availableChanged();
    }
}
```

## 2. Ask

The request - every field optional:

```json
{ "version": 1, "app": "Tube" }
```

| Field | |
|---|---|
| `version` | `1` |
| `app` | your app's name as the user knows it, in the user's language - the question shows it. One line, up to 60 characters (longer is cut). Missing - the install id stands there instead |

```cpp
void BBMusicSignIn::ask()
{
    QVariantMap body;
    body["version"] = 1;
    body["app"] = tr("Tube");

    QByteArray json;
    bb::data::JsonDataAccess().saveToBuffer(QVariant(body), &json);

    bb::system::InvokeRequest request;
    request.setTarget(QString::fromLatin1(kBBMusicTarget));
    request.setAction(QString::fromLatin1(kBBMusicAction));
    request.setMimeType(QString::fromLatin1("application/json"));
    request.setData(json);
    bb::system::InvokeTargetReply* reply = m_invoke->invoke(request);
    if (reply == 0) {
        emit refused(QString::fromLatin1("no-card"));
        return;
    }
    m_waiting = true;
    connect(reply, SIGNAL(finished()), this, SLOT(onInvokeFinished()));
}

void BBMusicSignIn::onInvokeFinished()
{
    bb::system::InvokeTargetReply* reply = qobject_cast<bb::system::InvokeTargetReply*>(sender());
    reply->deleteLater();
    if (reply->error() != bb::system::InvokeReplyError::None) {
        // The card did not come up: BBMusic was removed since check(), or the system said no.
        m_waiting = false;
        emit refused(QString::fromLatin1("no-card"));
    }
}
```

## 3. The answer

`childCardDone`, `dataType()` `application/json`, UTF-8:

| `reason()` | `data()` | What happened |
|---|---|---|
| `done` | `{ "version": 1, "refreshToken": "...", "clientId": "...", "clientSecret": "...", "scope": "https://www.googleapis.com/auth/youtube https://www.googleapis.com/auth/youtube-paid-content", "tokenUrl": "https://oauth2.googleapis.com/token" }` | the user allowed |
| `cancel` | `{ "version": 1 }` | the user said no |
| `cancel` | `{ "version": 1, "error": "not-signed-in" }` | BBMusic is not signed in to YouTube - nothing to give |
| `cancel` | `{ "version": 1, "error": "bad-request" }` | the request was not a JSON object |
| `cancel` | `{ "version": 1, "error": "not-allowed" }` | the caller is not Tube. The one answer Tube's own card does not have |
| anything else, or none | - | the system closed the card (the user swiped it away, went elsewhere): nothing came |

Handle both outcomes - a refusal is as ordinary as a yes:

```cpp
void BBMusicSignIn::onChildCardDone(const bb::system::CardDoneMessage& message)
{
    if (!m_waiting) {
        return;                         // another card of this app, not BBMusic's
    }
    m_waiting = false;
    bb::data::JsonDataAccess json;
    const QVariantMap answer = json.loadFromBuffer(message.data()).toMap();
    if (message.reason() == QLatin1String("done") && !json.hasError()
            && !answer.value("refreshToken").toString().isEmpty()) {
        emit signedIn(answer);          // keep it and go on signed in
        return;
    }
    // "cancel", or the system closed the card: nothing came. "error" says why when BBMusic knows:
    // "not-signed-in" - say "Sign in to BBMusic first" (or offer your own sign-in); "" - the user said no.
    emit refused(answer.value("error").toString());
}
```

**What `done` gives:** a Google OAuth 2.0 refresh token of the client in `clientId` / `clientSecret` (Google's
"YouTube on TV" client, as BBMusic read it from `youtube.com/tv` when it signed in), with `scope`. Access tokens
come from `tokenUrl` with the standard `refresh_token` grant; requests carry them as `Authorization: Bearer`.
Checked on 2026-11-21 with a token BBMusic's own sign-in made: `tokenUrl` answers 200 with an access token and
both scopes, and no new refresh token. Treat the refresh token as a password: keep it in your app's own sandbox,
never in a shared folder, never in a log. With `done` every field of the table is there; a later BBMusic may add
fields to `version` 1 - read the ones you know.

## Things to know

- **One sign-in, two apps - and signing out in BBMusic ends both.** Both use the same refresh token. When the
  user signs out in BBMusic, BBMusic revokes the token at Google, and that ends your app's access too: you then
  get `invalid_grant` from `tokenUrl` - forget the token and offer the card again. This is not what Tube does on
  its side (it forgets its copy and revokes nothing). Taking the access back in the Google account ends both as
  well.
- **The account BBMusic holds may be Tube's own.** When BBMusic was signed in through Tube's card, its card
  gives that same token back, and `scope` is then `https://www.googleapis.com/auth/youtube` alone. Signing out
  in BBMusic revokes that one too.
- **BBMusic does not follow the other app.** It keeps the account it has until the user signs out in BBMusic or
  takes another one; an account switch in Tube changes nothing here.
- **Tube only, for now.** The card looks at the caller's install id and answers one that begins
  `doctorrokter.BBTube.`; any other caller sees why and gets `not-allowed`. Only the package's name is checked:
  the full install id of the signed Tube would tie the card to its developer's signing key.
- **The card uses no network** and writes nothing. It asks BBMusic's engine, which a first invoke may have to
  start (a few seconds; it stops by itself after ten idle minutes); the card's process is pooled between
  invokes. BBMusic's log has the caller's install id and the outcome, never a token.
- **No card, no entry.** A BBMusic older than 1.0.2 has no card: `check()` finds no target, and your app shows
  no entry.
- **State, 2026-11-25:** tried on a phone with Tube and working.
