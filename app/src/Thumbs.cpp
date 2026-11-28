#include "Thumbs.h"

#include "EngineLink.h"

#include <QColor>
#include <QImage>
#include <QImageReader>
#include <QUrl>

static QString keyOf(const QString &url, int size)
{
    return QString::number(size) + "|" + url;
}

Thumbs::Thumbs(EngineLink *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(0);
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(flush()));
    connect(engine, SIGNAL(reply(QString,bool,QVariant,QString,QString)),
            this, SLOT(onReply(QString,bool,QVariant,QString,QString)));
    connect(engine, SIGNAL(event(QString,QVariantMap)), this, SLOT(onEvent(QString,QVariantMap)));
    connect(engine, SIGNAL(connectedChanged()), this, SLOT(onConnectedChanged()));
}

QString Thumbs::file(const QString &url, int size)
{
    if (url.isEmpty())
        return QString();
    const QString key = keyOf(url, size);
    const QHash<QString, QString>::const_iterator known = m_files.constFind(key);
    if (known != m_files.constEnd())
        return known.value();
    if (!m_asked.contains(key)) {
        m_asked.insert(key);
        m_pending[size].append(url);
        m_timer.start();
    }
    return QString();
}

void Thumbs::flush()
{
    if (!m_engine->connected())
        return; // onConnectedChanged() comes back here
    for (QMap<int, QStringList>::const_iterator i = m_pending.constBegin(); i != m_pending.constEnd(); ++i) {
        // The engine looks at 200 urls of a request at most.
        for (int from = 0; from < i.value().size(); from += 200) {
            QVariantMap fields;
            fields.insert("urls", QVariant(QStringList(i.value().mid(from, 200))));
            fields.insert("size", i.key());
            const QString id = m_engine->request("thumbs", fields);
            if (!id.isEmpty())
                m_requests.insert(id, i.key());
        }
    }
    m_pending.clear();
}

void Thumbs::store(const QString &url, int size, const QString &path)
{
    if (path.isEmpty())
        return;
    const QString key = keyOf(url, size);
    const QString fileUrl = "file://" + path;
    m_asked.remove(key);
    if (m_files.value(key) == fileUrl)
        return;
    m_files.insert(key, fileUrl);
    emit ready(url, size, fileUrl);
}

void Thumbs::onReply(QString id, bool ok, QVariant result, QString, QString)
{
    const QHash<QString, int>::iterator request = m_requests.find(id);
    if (request == m_requests.end())
        return;
    const int size = request.value();
    m_requests.erase(request);
    if (!ok)
        return;
    const QVariantMap files = result.toMap().value("files").toMap();
    for (QVariantMap::const_iterator i = files.constBegin(); i != files.constEnd(); ++i)
        store(i.key(), size, i.value().toString());
}

void Thumbs::onEvent(QString name, QVariantMap fields)
{
    if (name == "thumb")
        store(fields.value("url").toString(), fields.value("size").toInt(), fields.value("file").toString());
}

// A new engine has forgotten what it was asked: what is still unanswered is asked again once
// the link is back (the rows that wait for it will not call file() a second time).
void Thumbs::onConnectedChanged()
{
    if (m_engine->connected()) {
        flush();
        return;
    }
    m_requests.clear();
    foreach (const QString &key, m_asked) {
        const int bar = key.indexOf('|');
        const QString url = key.mid(bar + 1);
        QStringList &list = m_pending[key.left(bar).toInt()];
        if (!list.contains(url))
            list.append(url);
    }
}

QString Thumbs::tint(const QString &fileUrl) const
{
    // A JPEG decodes straight to 16 x 16: no full-size picture in memory for an average.
    QImageReader reader(QUrl(fileUrl).toLocalFile());
    reader.setScaledSize(QSize(16, 16));
    const QImage image = reader.read();
    if (image.isNull())
        return QString();
    qint64 r = 0, g = 0, b = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb p = image.pixel(x, y);
            r += qRed(p);
            g += qGreen(p);
            b += qBlue(p);
        }
    }
    const int n = image.width() * image.height();
    QColor c(int(r / n), int(g / n), int(b / n));
    // ponytail: a plain average, often muddy; take the most saturated cluster if it looks dull.
    qreal h, s, l;
    c.getHslF(&h, &s, &l);
    c.setHslF(h < 0 ? 0 : h, qMin(qreal(1), s * qreal(1.3)), qreal(0.38));
    return c.name();
}
