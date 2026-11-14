#ifndef ENGINELINK_H
#define ENGINELINK_H

#include <bb/system/InvokeManager>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

// The UI's side of docs/protocol.md: starts the engine through its invoke target, reads
// engine.json, connects to 127.0.0.1, says hello with the token, then speaks NDJSON. When the link
// drops or the engine is not there it starts the engine again: every 300 ms for 5 s, then every 5 s.
class EngineLink : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
public:
    explicit EngineLink(QObject *parent = 0);
    ~EngineLink();

    bool connected() const { return m_authed; }

    // Begins connecting (and keeps at it).
    Q_INVOKABLE void start();
    // The "Retry" button: a new round of quick attempts now.
    Q_INVOKABLE void retry();
    // Sends {"@":<fresh id>,"cmd":cmd,...fields} and returns the id; "" when not connected.
    Q_INVOKABLE QString request(const QString &cmd, const QVariantMap &fields);

signals:
    void connectedChanged();
    void reply(QString id, bool ok, QVariant result, QString errorCode, QString errorMessage);
    // The engine's events. Also two made-up ones, from the status and queue this class asks for
    // after connecting: "status" (fields = the status result) and "queue_snapshot" (fields = the
    // queue result).
    void event(QString name, QVariantMap fields);

private slots:
    void attempt();
    void socketConnected();
    void readLines();
    void socketError();
    void lost();

private:
    void startEngine();
    void writeLine(const QByteArray &json);
    void handleLine(const QByteArray &line);
    void scheduleAttempt();

    QTcpSocket m_socket;
    QTimer m_retry;
    QElapsedTimer m_began;
    QElapsedTimer m_invokedAt;
    bb::system::InvokeManager *m_invoker;
    QString m_token;
    bool m_authed;
    int m_seq;
    QString m_statusId;
    QString m_queueId;
};

#endif
