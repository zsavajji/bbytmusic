#ifndef TUBESIGNIN_H
#define TUBESIGNIN_H

#include <QObject>
#include <QString>
#include <QVariant>

namespace bb {
namespace system {
class CardDoneMessage;
class InvokeManager;
}
}

class EngineLink;

// "Sign in with Tube": opens the sign-in card of the Tube app (1.17.0 and later; the contract is
// its developer's) and hands the card's answer to the engine. The token goes from the card
// straight to the engine and is never given to QML or to the log.
class TubeSignIn : public QObject
{
    Q_OBJECT
    // Tube's card is installed.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // From ask() until the card's answer has been dealt with, the engine's import included.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    TubeSignIn(EngineLink *engine, QObject *parent = 0);

    bool available() const { return m_available; }
    bool busy() const { return m_waiting || !m_importId.isEmpty(); }

    Q_INVOKABLE void check();
    Q_INVOKABLE void ask();

signals:
    void availableChanged();
    void busyChanged();
    // Nothing came: "" (the user said no), "not-signed-in", "bad-request", "no-card",
    // "disconnected", or the engine's "network" / "refused".
    void refused(QString error);

private slots:
    void onQueryFinished();
    void onInvokeFinished();
    void onChildCardDone(const bb::system::CardDoneMessage &message);
    void onReply(QString id, bool ok, QVariant result, QString errorCode, QString errorMessage);

private:
    EngineLink *m_engine;
    bb::system::InvokeManager *m_invoke;
    bool m_available;
    bool m_waiting;         // a card of ours is up: the next childCardDone is its answer
    QString m_importId;     // the engine's import, asked and not answered yet
};

#endif
