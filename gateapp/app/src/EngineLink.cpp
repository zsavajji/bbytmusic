#include "EngineLink.h"

#include <bb/data/JsonDataAccess>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTargetReply>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QVariantMap>

#include "UiLog.h"

using namespace bb::data;
using namespace bb::system;

static const int GiveUpMs = 5000;
static const int RetryMs = 300;
static const int ReinvokeMs = 1500; // a START that finds the engine running does nothing

EngineLink::EngineLink(QObject *parent)
    : QObject(parent)
    , m_invoker(new InvokeManager(this))
    , m_authed(false)
{
    // Qt 4.8's QElapsedTimer holds garbage until started or invalidated.
    m_began.invalidate();
    m_invokedAt.invalidate();
    m_retry.setSingleShot(true);
    connect(&m_retry, SIGNAL(timeout()), this, SLOT(attempt()));
    connect(&m_socket, SIGNAL(connected()), this, SLOT(socketConnected()));
    connect(&m_socket, SIGNAL(readyRead()), this, SLOT(readLines()));
    connect(&m_socket, SIGNAL(disconnected()), this, SLOT(lost()));
    connect(&m_socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(socketError()));
}

EngineLink::~EngineLink()
{
    m_socket.disconnect(this);
}

void EngineLink::say(const QString &text)
{
    uiLog(text);
    emit line(text);
}

void EngineLink::start()
{
    if (m_authed) {
        say("already connected");
        return;
    }
    m_began.start();
    attempt();
}

void EngineLink::attempt()
{
    if (m_authed)
        return;
    if (m_began.elapsed() > GiveUpMs) {
        say("gave up: no engine within 5 s");
        return;
    }
    if (!m_invokedAt.isValid() || m_invokedAt.elapsed() > ReinvokeMs)
        startEngine();
    m_socket.abort();
    int port = 0;
    QFile file(QDir::homePath() + "/engine.json");
    if (file.open(QIODevice::ReadOnly)) {
        JsonDataAccess json;
        const QVariant doc = json.loadFromBuffer(file.readAll());
        if (!json.hasError() && doc.type() == QVariant::Map) {
            const QVariantMap map = doc.toMap();
            port = map["port"].toInt();
            m_token = map["token"].toString();
        }
    }
    if (port > 0)
        m_socket.connectToHost(QHostAddress(QHostAddress::LocalHost), port);
    else
        m_retry.start(RetryMs);
}

void EngineLink::startEngine()
{
    InvokeRequest request;
    request.setTarget("com.zsavajji.bbmusic.gate.service");
    request.setAction("com.zsavajji.bbmusic.gate.START");
    InvokeTargetReply *reply = m_invoker->invoke(request);
    m_invokedAt.start();
    if (!reply) {
        say("no reply from the system to START");
        return;
    }
    reply->setParent(this);
    say("engine started (START sent)");
}

void EngineLink::socketConnected()
{
    send(QString("{\"@\":\"hello\",\"cmd\":\"hello\",\"token\":\"%1\"}").arg(m_token));
}

void EngineLink::socketError()
{
    if (m_authed)
        return; // lost() reports it
    say("connect failed: " + m_socket.errorString());
    m_retry.start(RetryMs);
}

void EngineLink::readLines()
{
    while (m_socket.canReadLine()) {
        const QString text = QString::fromUtf8(m_socket.readLine()).trimmed();
        if (text.isEmpty())
            continue;
        if (!m_authed && text.contains("\"@\":\"hello\"")) {
            if (text.contains("\"ok\":true")) {
                m_authed = true;
                say("connected");
            } else {
                say("hello refused: " + text);
            }
            continue;
        }
        say(text);
    }
}

void EngineLink::lost()
{
    const bool was = m_authed;
    m_authed = false;
    if (was)
        say("disconnected");
}

void EngineLink::send(const QString &jsonLine)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState) {
        say("not connected");
        return;
    }
    if (m_authed)
        say("> " + jsonLine);
    m_socket.write(jsonLine.toUtf8() + "\n");
}
