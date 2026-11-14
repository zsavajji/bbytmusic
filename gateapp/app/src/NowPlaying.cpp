#include "NowPlaying.h"

#include <bb/multimedia/MediaError>
#include <bb/multimedia/MediaState>
#include <bb/multimedia/MetaData>
#include <bb/multimedia/OverlayStyle>
#include <QVariantMap>

#include "UiLog.h"

using namespace bb::multimedia;

NowPlaying::NowPlaying(QObject *parent)
    : QObject(parent)
    , m_npc(new NowPlayingConnection("bbmusic-gate", this))
{
    m_npc->setOverlayStyle(OverlayStyle::Fancy);
    connect(m_npc, SIGNAL(acquired()), this, SLOT(onAcquired()));
    connect(m_npc, SIGNAL(revoked()), this, SLOT(onRevoked()));
    connect(m_npc, SIGNAL(play()), this, SLOT(onPlay()));
    connect(m_npc, SIGNAL(pause()), this, SLOT(onPause()));
    connect(m_npc, SIGNAL(next()), this, SLOT(onNext()));
    connect(m_npc, SIGNAL(previous()), this, SLOT(onPrevious()));
}

void NowPlaying::describe()
{
    QVariantMap metadata;
    metadata[MetaData::Title] = "BBMusic gate";
    metadata[MetaData::Artist] = "Test tone";
    m_npc->setMetaData(metadata);
    m_npc->setDuration(600000);
    m_npc->setNextEnabled(true);
    m_npc->setPreviousEnabled(true);
    m_npc->setMediaState(MediaState::Started);
}

void NowPlaying::acquire()
{
    uiLog("np: acquire() called");
    describe();
    const MediaError::Type error = m_npc->acquire();
    announce(QString("acquire() returned %1").arg(int(error)));
}

void NowPlaying::revoke()
{
    uiLog("np: revoke() called");
    m_npc->revoke();
}

void NowPlaying::setPlaying(bool playing)
{
    uiLog(playing ? "np: state Started" : "np: state Paused");
    m_npc->setMediaState(playing ? MediaState::Started : MediaState::Paused);
}

void NowPlaying::announce(const QString &name)
{
    uiLog("np: " + name);
    emit event(name);
}

void NowPlaying::onAcquired()
{
    describe(); // some stacks only take the state once acquired
    announce("acquired");
}
void NowPlaying::onRevoked() { announce("revoked"); }
void NowPlaying::onPlay() { announce("play"); }
void NowPlaying::onPause() { announce("pause"); }
void NowPlaying::onNext() { announce("next"); }
void NowPlaying::onPrevious() { announce("previous"); }
