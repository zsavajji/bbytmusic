#ifndef NOWPLAYING_H
#define NOWPLAYING_H

#include <bb/multimedia/NowPlayingConnection>
#include <QObject>
#include <QString>

// Lock-screen controls and media keys through NowPlayingConnection, with no MediaPlayer behind
// it (gate 0.5): the engine owns the speaker, this only claims the controls.
class NowPlaying : public QObject
{
    Q_OBJECT
public:
    explicit NowPlaying(QObject *parent = 0);

    Q_INVOKABLE void acquire();
    Q_INVOKABLE void revoke();
    // The overlay draws play or pause from the state it is told, and sends the other one.
    Q_INVOKABLE void setPlaying(bool playing);

signals:
    // "acquired", "revoked", "play", "pause", "next" or "previous".
    void event(const QString &name);

private slots:
    void onAcquired();
    void onRevoked();
    void onPlay();
    void onPause();
    void onNext();
    void onPrevious();

private:
    void announce(const QString &name);
    void describe();

    bb::multimedia::NowPlayingConnection *m_npc;
};

#endif
