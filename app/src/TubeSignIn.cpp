#include "TubeSignIn.h"

#include <bb/data/JsonDataAccess>
#include <bb/system/CardDoneMessage>
#include <bb/system/InvokeAction>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeQueryTargetsReply>
#include <bb/system/InvokeQueryTargetsRequest>
#include <bb/system/InvokeReplyError>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTarget>
#include <bb/system/InvokeTargetReply>
#include <QList>
#include <QVariantMap>

#include "EngineLink.h"
#include "UiLog.h"

static const char *const TubeTarget = "doctorrokter.BBTube.card.signin";
static const char *const TubeAction = "doctorrokter.BBTube.SHARE_SIGN_IN";

TubeSignIn::TubeSignIn(EngineLink *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_invoke(new bb::system::InvokeManager(this))
    , m_available(false)
    , m_waiting(false)
{
    connect(m_invoke, SIGNAL(childCardDone(const bb::system::CardDoneMessage&)),
            this, SLOT(onChildCardDone(const bb::system::CardDoneMessage&)));
    connect(m_engine, SIGNAL(reply(QString,bool,QVariant,QString,QString)),
            this, SLOT(onReply(QString,bool,QVariant,QString,QString)));
}

void TubeSignIn::check()
{
    bb::system::InvokeQueryTargetsRequest request;
    request.setAction(QString::fromLatin1(TubeAction));
    request.setMimeType(QString::fromLatin1("application/json"));
    request.setTargetTypes(bb::system::InvokeTarget::Card);
    request.setActionType(bb::system::InvokeAction::All);
    bb::system::InvokeQueryTargetsReply *reply = m_invoke->queryTargets(request);
    if (!reply)
        return;
    connect(reply, SIGNAL(finished()), this, SLOT(onQueryFinished()));
}

void TubeSignIn::onQueryFinished()
{
    bb::system::InvokeQueryTargetsReply *reply = qobject_cast<bb::system::InvokeQueryTargetsReply *>(sender());
    reply->deleteLater();
    bool installed = false;
    if (reply->error() == bb::system::InvokeReplyError::None) {
        const QList<bb::system::InvokeAction> actions = reply->actions();
        for (int i = 0; i < actions.size(); ++i) {
            const QList<bb::system::InvokeTarget> targets = actions.at(i).targets();
            for (int j = 0; j < targets.size(); ++j) {
                if (targets.at(j).name() == QLatin1String(TubeTarget))
                    installed = true;
            }
        }
    }
    if (installed != m_available) {
        m_available = installed;
        uiLog(installed ? "tube: card installed" : "tube: no card");
        emit availableChanged();
    }
}

void TubeSignIn::ask()
{
    if (busy())
        return;
    QVariantMap body;
    body["version"] = 1;
    body["app"] = "BBMusic";
    QByteArray json;
    bb::data::JsonDataAccess().saveToBuffer(QVariant(body), &json);

    bb::system::InvokeRequest request;
    request.setTarget(QString::fromLatin1(TubeTarget));
    request.setAction(QString::fromLatin1(TubeAction));
    request.setMimeType(QString::fromLatin1("application/json"));
    request.setData(json);
    bb::system::InvokeTargetReply *reply = m_invoke->invoke(request);
    if (!reply) {
        emit refused("no-card");
        return;
    }
    uiLog("tube: asked");
    m_waiting = true;
    emit busyChanged();
    connect(reply, SIGNAL(finished()), this, SLOT(onInvokeFinished()));
}

void TubeSignIn::onInvokeFinished()
{
    bb::system::InvokeTargetReply *reply = qobject_cast<bb::system::InvokeTargetReply *>(sender());
    reply->deleteLater();
    if (reply->error() != bb::system::InvokeReplyError::None) {
        // The card did not come up: Tube was removed since check(), or the system said no.
        uiLog("tube: the card did not open");
        m_waiting = false;
        emit busyChanged();
        emit refused("no-card");
    }
}

void TubeSignIn::onChildCardDone(const bb::system::CardDoneMessage &message)
{
    if (!m_waiting)
        return; // another card of this app, not Tube's
    m_waiting = false;
    bb::data::JsonDataAccess json;
    const QVariantMap answer = json.loadFromBuffer(message.data()).toMap();
    uiLog("tube: answer " + message.reason() + " " + answer.value("error").toString());
    if (message.reason() == QLatin1String("done") && !json.hasError()
            && !answer.value("refreshToken").toString().isEmpty()) {
        // The token goes to the engine and nowhere else.
        QVariantMap import;
        import["refresh_token"] = answer.value("refreshToken").toString();
        const QString clientId = answer.value("clientId").toString();
        const QString clientSecret = answer.value("clientSecret").toString();
        if (!clientId.isEmpty() && !clientSecret.isEmpty()) {
            import["client_id"] = clientId;
            import["client_secret"] = clientSecret;
        }
        QVariantMap fields;
        fields["import"] = import;
        m_importId = m_engine->request("account", fields);
        if (m_importId.isEmpty())
            emit refused("disconnected");
        emit busyChanged();
        return;
    }
    // "cancel", or the system closed the card. "" means the user said no.
    emit busyChanged();
    emit refused(answer.value("error").toString());
}

void TubeSignIn::onReply(QString id, bool ok, QVariant, QString errorCode, QString)
{
    if (id.isEmpty() || id != m_importId)
        return;
    m_importId.clear();
    emit busyChanged();
    // On success the engine's account event tells Settings.
    if (!ok)
        emit refused(errorCode);
}
