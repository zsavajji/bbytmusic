#ifndef PLAYERSTATE_H
#define PLAYERSTATE_H

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

// What QML shows of the player, kept current from the engine's state, track, position, queue and
// error events and the status reply. Between position events (about 1 s apart) it advances the
// position itself while playing, so a scrubber moves smoothly.
class PlayerState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    // True when there is something to show in the mini-player.
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(int index READ index NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(int positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(int durationMs READ durationMs NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int queueLength READ queueLength NOTIFY changed)
public:
    explicit PlayerState(QObject *parent = 0);

    QString state() const { return m_state; }
    bool playing() const { return m_state == "playing"; }
    bool loaded() const { return !m_title.isEmpty() && (m_state != "stopped" || m_queueLength > 0); }
    int index() const { return m_index; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    int positionMs() const { return m_positionMs; }
    int durationMs() const { return m_durationMs; }
    QString error() const { return m_error; }
    int queueLength() const { return m_queueLength; }

    // 215000 -> "3:35", 3725000 -> "1:02:05".
    Q_INVOKABLE QString fmt(int ms) const;

public slots:
    void onEvent(QString name, QVariantMap fields);

signals:
    void changed();
    void positionChanged();
    // An "error" event: a track could not be played.
    void errorOccurred(QString message);

private slots:
    void tick();

private:
    void setTrack(const QVariantMap &track);
    void setPosition(const QVariantMap &fields);

    QString m_state;
    int m_index;
    QString m_title, m_artist, m_album, m_error;
    int m_positionMs, m_durationMs, m_queueLength;
    QTimer m_tick;
    QElapsedTimer m_clock;
};

#endif
