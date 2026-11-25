#include "SignInCard.h"

#include <bb/data/JsonDataAccess>
#include <bb/system/CardDoneMessage>
#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeSource>
#include <QByteArray>

#include "EngineLink.h"
#include "UiLog.h"

static const char *const CardTarget = "com.zsavajji.bbmusic.card.signin";
static const char *const AllowedPrefix = "doctorrokter.BBTube.";
static const char *const TokenUrl = "https://oauth2.googleapis.com/token";
static const char *const ScopeYoutube = "https://www.googleapis.com/auth/youtube";
static const char *const ScopePaid = "https://www.googleapis.com/auth/youtube-paid-content";

SignInCard::SignInCard(EngineLink *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_invoke(new bb::system::InvokeManager(this))
    , m_state("loading")
{
    connect(m_invoke, SIGNAL(invoked(const bb::system::InvokeRequest&)),
            this, SLOT(onInvoked(const bb::system::InvokeRequest&)));
    connect(m_invoke, SIGNAL(cardPooled(const bb::system::CardDoneMessage&)),
            this, SLOT(onPooled(const bb::system::CardDoneMessage&)));
    connect(m_engine, SIGNAL(connectedChanged()), this, SLOT(onConnected()));
    connect(m_engine, SIGNAL(reply(QString,bool,QVariant,QString,QString)),
            this, SLOT(onReply(QString,bool,QVariant,QString,QString)));
}

void SignInCard::onInvoked(const bb::system::InvokeRequest &request)
{
    if (request.target() != QLatin1String(CardTarget))
        return;
    reset();
    m_installId = request.source().installId();
    uiLog("card: asked by " + m_installId);
    if (!m_installId.startsWith(QLatin1String(AllowedPrefix))) {
        m_state = "not-allowed";
        emit changed();
        return;
    }
    bb::data::JsonDataAccess json;
    const QVariant data = json.loadFromBuffer(request.data());
    if (json.hasError() || data.type() != QVariant::Map) {
        m_state = "bad-request";
        emit changed();
        return;
    }
    m_app = data.toMap().value("app").toString().simplified().left(60);
    if (m_app.isEmpty())
        m_app = m_installId;
    emit changed();
    askAccount();
}

void SignInCard::onPooled(const bb::system::CardDoneMessage &)
{
    reset();
}

void SignInCard::onConnected()
{
    if (m_engine->connected() && m_state == "loading" && !m_installId.isEmpty() && !m_app.isEmpty()
            && m_accountId.isEmpty())
        askAccount();
}

void SignInCard::askAccount()
{
    // Empty when not connected yet: onConnected() asks then.
    m_accountId = m_engine->request("account", QVariantMap());
}

void SignInCard::onReply(QString id, bool ok, QVariant result, QString errorCode, QString)
{
    if (id.isEmpty())
        return;
    if (id == m_accountId) {
        m_accountId.clear();
        const QVariantMap account = result.toMap();
        if (ok && account.value("state").toString() == "in") {
            m_account = account.value("name").toString();
            m_state = "ready";
        } else if (!ok && errorCode == "disconnected") {
            return; // the reconnect asks again, through onConnected()
        } else {
            m_state = "not-signed-in";
        }
        emit changed();
    } else if (id == m_exportId) {
        m_exportId.clear();
        const QVariantMap data = result.toMap();
        if (ok && !data.value("refresh_token").toString().isEmpty()) {
            QVariantMap body;
            body["version"] = 1;
            body["refreshToken"] = data.value("refresh_token").toString();
            body["clientId"] = data.value("client_id").toString();
            body["clientSecret"] = data.value("client_secret").toString();
            body["scope"] = data.value("source").toString() == "tube"
                    ? QString::fromLatin1(ScopeYoutube)
                    : QString::fromLatin1(ScopeYoutube) + " " + QString::fromLatin1(ScopePaid);
            body["tokenUrl"] = QString::fromLatin1(TokenUrl);
            finish("done", body);
            return;
        }
        if (!ok && errorCode == "disconnected")
            return;
        m_state = "not-signed-in";
        emit changed();
    }
}

void SignInCard::allow()
{
    if (m_state != "ready" || !m_exportId.isEmpty())
        return;
    QVariantMap fields;
    fields["export"] = true;
    m_exportId = m_engine->request("account", fields);
}

void SignInCard::cancel()
{
    QVariantMap body;
    body["version"] = 1;
    if (m_state == "not-signed-in" || m_state == "bad-request" || m_state == "not-allowed")
        body["error"] = m_state;
    finish("cancel", body);
}

void SignInCard::finish(const QString &reason, const QVariantMap &body)
{
    QByteArray buffer;
    bb::data::JsonDataAccess().saveToBuffer(QVariant(body), &buffer);
    bb::system::CardDoneMessage message;
    message.setReason(reason);
    message.setDataType("application/json");
    message.setData(QString::fromUtf8(buffer));
    m_invoke->sendCardDone(message);
    uiLog("card: " + reason + " " + body.value("error").toString());
    reset();
}

void SignInCard::reset()
{
    m_state = "loading";
    m_app.clear();
    m_installId.clear();
    m_account.clear();
    m_accountId.clear();
    m_exportId.clear();
    emit changed();
}
