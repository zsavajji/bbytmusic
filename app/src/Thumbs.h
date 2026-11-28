#ifndef THUMBS_H
#define THUMBS_H

#include <QHash>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

class EngineLink;

// Artwork for QML. The UI makes no network request (Qt 4.8 cannot speak today's TLS): the engine
// fetches a thumbnail into data/thumbs/ and this class hands QML the file. file() answers at once
// with what is known; what is not is asked of the engine in one `thumbs` command per turn of the
// event loop, and ready() follows for each as it arrives.
class Thumbs : public QObject
{
    Q_OBJECT
public:
    Thumbs(EngineLink *engine, QObject *parent = 0);

    // "file:///…" when the thumbnail is in the cache, else "". size: the side in pixels to ask
    // for (0: as the url says).
    Q_INVOKABLE QString file(const QString &url, int size);

    // The picture's average colour at a middle lightness, "#rrggbb", for a backdrop that both
    // white and dark text read on (docked mode's lyrics); "" when the file cannot be read.
    Q_INVOKABLE QString tint(const QString &fileUrl) const;

signals:
    void ready(QString url, int size, QString file);

private slots:
    void flush();
    void onReply(QString id, bool ok, QVariant result, QString errorCode, QString errorMessage);
    void onEvent(QString name, QVariantMap fields);
    void onConnectedChanged();

private:
    void store(const QString &url, int size, const QString &path);

    EngineLink *m_engine;
    QHash<QString, QString> m_files;      // "<size>|<url>" -> file url
    QSet<QString> m_asked;                // the same keys: asked, not yet answered
    QMap<int, QStringList> m_pending;     // by size: to ask at the next flush
    QHash<QString, int> m_requests;       // request id -> size
    QTimer m_timer;
};

#endif
