#include "PlayerState.h"

PlayerState::PlayerState(QObject *parent)
    : QObject(parent)
    , m_state("stopped")
    , m_index(-1)
    , m_repeat("off")
    , m_eq("off")
    , m_shuffle(false)
    , m_positionMs(0)
    , m_durationMs(0)
    , m_queueLength(0)
{
    m_clock.start();
    m_tick.setInterval(250);
    connect(&m_tick, SIGNAL(timeout()), this, SLOT(tick()));
    m_tick.start();
}

QString PlayerState::fmt(int ms) const
{
    int s = ms > 0 ? ms / 1000 : 0;
    const int h = s / 3600;
    s %= 3600;
    const QString ss = QString("%1").arg(s % 60, 2, 10, QLatin1Char('0'));
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(s / 60, 2, 10, QLatin1Char('0')).arg(ss);
    return QString("%1:%2").arg(s / 60).arg(ss);
}

void PlayerState::tick()
{
    const qint64 delta = m_clock.restart();
    if (m_state != "playing")
        return;
    m_positionMs += int(delta);
    if (m_durationMs > 0 && m_positionMs > m_durationMs)
        m_positionMs = m_durationMs;
    emit positionChanged();
}

void PlayerState::setPosition(const QVariantMap &fields)
{
    if (fields.contains("position_ms"))
        m_positionMs = fields["position_ms"].toInt();
    if (fields.contains("duration_ms"))
        m_durationMs = fields["duration_ms"].toInt();
    m_clock.restart();
}

void PlayerState::setTrack(const QVariantMap &track)
{
    m_track = track;
    m_title = track["title"].toString();
    m_artist = track["artist"].toString();
    m_album = track["album"].toString();
    if (track["duration_s"].toInt() > 0)
        m_durationMs = track["duration_s"].toInt() * 1000;
}

void PlayerState::onEvent(QString name, QVariantMap f)
{
    if (name == "state" || name == "status") {
        m_state = f["state"].toString();
        if (f.contains("index"))
            m_index = f["index"].toInt();
        m_error = f["error"].toString();
        if (name == "status") {
            if (f["track"].type() == QVariant::Map)
                setTrack(f["track"].toMap());
            else
                setTrack(QVariantMap());
            m_queueLength = f["queue_len"].toInt();
            if (f.contains("repeat"))
                m_repeat = f["repeat"].toString();
            if (f.contains("eq"))
                m_eq = f["eq"].toString();
            if (f.contains("shuffle"))
                m_shuffle = f["shuffle"].toBool();
        }
        setPosition(f);
    } else if (name == "track") {
        m_index = f["index"].toInt();
        m_durationMs = 0; // the track's own duration, or the next position event's
        setTrack(f["track"].toMap());
        m_positionMs = 0;
        m_clock.restart();
    } else if (name == "position") {
        setPosition(f);
        emit positionChanged();
        return;
    } else if (name == "queue") {
        m_queueLength = f["len"].toInt();
        m_index = f["index"].toInt();
    } else if (name == "mode") {
        if (f.contains("repeat"))
            m_repeat = f["repeat"].toString();
        if (f.contains("eq"))
            m_eq = f["eq"].toString();
        if (f.contains("shuffle"))
            m_shuffle = f["shuffle"].toBool();
    } else if (name == "error") {
        emit errorOccurred(f["message"].toString());
        return;
    } else {
        return;
    }
    emit changed();
    emit positionChanged();
}
