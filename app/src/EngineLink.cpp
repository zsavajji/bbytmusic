#include "EngineLink.h"

#include <bb/data/JsonDataAccess>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeTargetReply>
#include <math.h>
#include <QDir>
#include <QFile>
#include <QHostAddress>

#include "UiLog.h"

using namespace bb::data;
using namespace bb::system;

static const int QuickMs = 5000; // quick attempts for this long after a start or a drop
static const int RetryMs = 300;
static const int SlowMs = 5000;
static const int ReinvokeMs = 1500; // a START that finds the engine running does nothing
static const int DownMs = 10000; // no link for this long: the engine is down, not waking

// JS numbers can reach C++ as doubles: whole ones must go out as 1, not 1.0.
static QVariant normalized(const QVariant &value)
{
    if (value.type() == QVariant::Double) {
        const double d = value.toDouble();
        if (d == floor(d) && fabs(d) < 2147483647.0)
            return QVariant(int(d));
    } else if (value.type() == QVariant::Map) {
        const QVariantMap in = value.toMap();
        QVariantMap out;
        for (QVariantMap::const_iterator i = in.constBegin(); i != in.constEnd(); ++i)
            out.insert(i.key(), normalized(i.value()));
        return out;
    } else if (value.type() == QVariant::List) {
        const QVariantList in = value.toList();
        QVariantList out;
        for (int i = 0; i < in.size(); ++i)
            out.append(normalized(in.at(i)));
        return out;
    }
    return value;
}

EngineLink::EngineLink(QObject *parent)
    : QObject(parent)
    , m_invoker(new InvokeManager(this))
    , m_authed(false)
    , m_down(false)
    , m_seq(0)
{
    // Qt 4.8's QElapsedTimer holds garbage until started or invalidated.
    m_began.invalidate();
    m_invokedAt.invalidate();
    m_retry.setSingleShot(true);
    connect(&m_retry, SIGNAL(timeout()), this, SLOT(attempt()));
    m_downTimer.setSingleShot(true);
    connect(&m_downTimer, SIGNAL(timeout()), this, SLOT(wentDown()));
    connect(&m_socket, SIGNAL(connected()), this, SLOT(socketConnected()));
    connect(&m_socket, SIGNAL(readyRead()), this, SLOT(readLines()));
    connect(&m_socket, SIGNAL(disconnected()), this, SLOT(lost()));
    connect(&m_socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(socketError()));
}

EngineLink::~EngineLink()
{
    m_socket.disconnect(this);
}

void EngineLink::start()
{
    if (m_authed)
        return;
    m_began.start();
    if (!m_down && !m_downTimer.isActive())
        m_downTimer.start(DownMs); // from the launch; a Retry does not start it again
    attempt();
}

void EngineLink::retry()
{
    if (m_authed)
        return;
    m_retry.stop();
    m_invokedAt.invalidate();
    start();
}

void EngineLink::scheduleAttempt()
{
    m_retry.start(m_began.isValid() && m_began.elapsed() > QuickMs ? SlowMs : RetryMs);
}

void EngineLink::attempt()
{
    if (m_authed)
        return;
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
        scheduleAttempt();
}

void EngineLink::startEngine()
{
    InvokeRequest request;
    request.setTarget("com.zsavajji.bbmusic.service");
    request.setAction("com.zsavajji.bbmusic.START");
    InvokeTargetReply *reply = m_invoker->invoke(request);
    m_invokedAt.start();
    if (!reply) {
        uiLog("no reply from the system to START");
        return;
    }
    reply->setParent(this);
    uiLog("engine START sent");
}

void EngineLink::socketConnected()
{
    writeLine(QString("{\"@\":\"h\",\"cmd\":\"hello\",\"token\":\"%1\",\"proto\":1}")
                  .arg(m_token).toUtf8());
}

void EngineLink::socketError()
{
    if (m_authed)
        return; // lost() reports it
    uiLog("connect failed: " + m_socket.errorString());
    scheduleAttempt();
}

void EngineLink::lost()
{
    const bool was = m_authed;
    m_authed = false;
    if (was) {
        uiLog("disconnected");
        m_began.start(); // quick attempts again
        m_downTimer.start(DownMs);
        emit connectedChanged();
    }
    // Whoever waits for an answer would wait for ever (a page's spinner, its request id).
    const QSet<QString> unanswered = m_pending;
    m_pending.clear();
    foreach (const QString &id, unanswered)
        emit reply(id, false, QVariant(), "disconnected", "The engine is not connected.");
    scheduleAttempt();
}

void EngineLink::wentDown()
{
    m_down = true;
    uiLog("engine not up after 10 s");
    emit downChanged();
}

void EngineLink::readLines()
{
    while (m_socket.canReadLine()) {
        const QByteArray line = m_socket.readLine().trimmed();
        if (!line.isEmpty())
            handleLine(line);
    }
}

void EngineLink::handleLine(const QByteArray &line)
{
    JsonDataAccess json;
    const QVariant doc = json.loadFromBuffer(line);
    if (json.hasError() || doc.type() != QVariant::Map)
        return;
    QVariantMap map = doc.toMap();
    // Not the positions (one a second) nor the thumbnails (one per row of a list).
    const QString ev = map.value("ev").toString();
    const QString id = map.value("@").toString();
    if (m_quiet.remove(id))
        uiLog("< " + id + " (not logged)");
    else if (ev != "position" && ev != "thumb")
        uiLog("< " + QString::fromUtf8(line.left(220)).trimmed());
    if (map.contains("ev")) {
        const QString name = map.take("ev").toString();
        emit event(name, map);
        return;
    }
    const bool ok = map["ok"].toBool();
    if (id == "h") {
        if (!ok) {
            uiLog("hello refused");
            return; // the engine closes the connection; lost() retries
        }
        m_authed = true;
        uiLog("connected");
        m_downTimer.stop();
        if (m_down) {
            m_down = false;
            emit downChanged();
        }
        emit connectedChanged();
        // A UI opened mid-song is then right (docs/protocol.md, last line).
        m_statusId = request("status", QVariantMap());
        m_queueId = request("queue", QVariantMap());
        return;
    }
    m_pending.remove(id);
    const QVariantMap error = map["error"].toMap();
    const QVariant result = map["result"];
    emit reply(id, ok, result, error["code"].toString(), error["message"].toString());
    if (ok && id == m_statusId)
        emit event("status", result.toMap());
    else if (ok && id == m_queueId)
        emit event("queue_snapshot", result.toMap());
}

void EngineLink::writeLine(const QByteArray &json, const QString &logAs)
{
    if (!logAs.isEmpty())
        uiLog("> " + logAs);
    else if (!json.contains("\"cmd\":\"hello\""))
        uiLog("> " + QString::fromUtf8(json.left(220)));
    m_socket.write(json + "\n");
}

QString EngineLink::request(const QString &cmd, const QVariantMap &fields)
{
    if (!m_authed || m_socket.state() != QAbstractSocket::ConnectedState)
        return QString();
    const QString id = QString::number(++m_seq);
    QVariantMap message = normalized(fields).toMap();
    message.insert("@", id);
    message.insert("cmd", cmd);
    QByteArray buffer;
    JsonDataAccess json;
    json.saveToBuffer(QVariant(message), &buffer);
    // JsonDataAccess may indent its output; inside strings newlines are escaped, so these are layout.
    buffer.replace('\n', "").replace('\r', "");
    // The Tube card's answer goes out here and the sign-in card's token comes back (export): the
    // log gets the command's name only, and the reply is not logged.
    QString logAs;
    if (fields.contains("import"))
        logAs = cmd + " import";
    else if (fields.contains("export")) {
        logAs = cmd + " export";
        m_quiet.insert(id);
    }
    writeLine(buffer, logAs);
    m_pending.insert(id);
    return id;
}
