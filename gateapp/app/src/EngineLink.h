#ifndef ENGINELINK_H
#define ENGINELINK_H

#include <bb/system/InvokeManager>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>

// The UI's side of the engine link (HyperBerry's, cut down): starts the engine through its invoke
// target, reads data/engine.json, connects to 127.0.0.1 and says hello with the token.
class EngineLink : public QObject
{
    Q_OBJECT
public:
    explicit EngineLink(QObject *parent = 0);
    ~EngineLink();

    // Invokes the headless target, then retries reading engine.json and connecting for 5 s.
    Q_INVOKABLE void start();
    // One JSON line to the engine; "not connected" in line() when there is no link.
    Q_INVOKABLE void send(const QString &jsonLine);

signals:
    // Every line the engine sent, and this class's own status messages.
    void line(const QString &text);

private slots:
    void attempt();
    void socketConnected();
    void readLines();
    void socketError();
    void lost();

private:
    void startEngine();
    void say(const QString &text);

    QTcpSocket m_socket;
    QTimer m_retry;
    QElapsedTimer m_began;
    QElapsedTimer m_invokedAt;
    bb::system::InvokeManager *m_invoker;
    QString m_token;
    bool m_authed;
};

#endif
