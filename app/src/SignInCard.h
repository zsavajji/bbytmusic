#ifndef SIGNINCARD_H
#define SIGNINCARD_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace bb {
namespace system {
class CardDoneMessage;
class InvokeManager;
class InvokeRequest;
}
}

class EngineLink;

// BBMusic's sign-in card, the mirror of Tube's (docs/signin-card.md): for now it answers the Tube
// app only. The token goes from the engine straight into the card's answer and is never given to
// QML or to the log.
class SignInCard : public QObject
{
    Q_OBJECT
    // "loading", "ready", "not-signed-in", "bad-request" or "not-allowed".
    Q_PROPERTY(QString state READ state NOTIFY changed)
    // The caller's name as it sent it.
    Q_PROPERTY(QString app READ app NOTIFY changed)
    // The caller as the system names it.
    Q_PROPERTY(QString installId READ installId NOTIFY changed)
    // The account's name.
    Q_PROPERTY(QString account READ account NOTIFY changed)
public:
    SignInCard(EngineLink *engine, QObject *parent = 0);

    QString state() const { return m_state; }
    QString app() const { return m_app; }
    QString installId() const { return m_installId; }
    QString account() const { return m_account; }

    Q_INVOKABLE void allow();
    Q_INVOKABLE void cancel();

signals:
    void changed();

private slots:
    void onInvoked(const bb::system::InvokeRequest &request);
    void onPooled(const bb::system::CardDoneMessage &message);
    void onConnected();
    void onReply(QString id, bool ok, QVariant result, QString errorCode, QString errorMessage);

private:
    void askAccount();
    void finish(const QString &reason, const QVariantMap &body);
    void reset();

    EngineLink *m_engine;
    bb::system::InvokeManager *m_invoke;
    QString m_state;
    QString m_app;
    QString m_installId;
    QString m_account;
    QString m_accountId;    // the engine's account question, asked and not answered yet
    QString m_exportId;     // the engine's export, asked and not answered yet
};

#endif
