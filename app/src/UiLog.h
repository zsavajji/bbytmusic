#ifndef UILOG_H
#define UILOG_H

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QString>

// Appends a timestamped line to bbmusic-ui.log in the app's data folder, so the UI's side (QML
// errors, the link's state) can be read after the app is gone. Never logs the engine's token.
inline void uiLog(const QString &text)
{
    QElapsedTimer took;
    took.start();
    QFile file(QDir::homePath() + "/bbmusic-ui.log");
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        // A slow open is the file system holding the main thread (2026-11-28: a 4 s stall to explain).
        const qint64 ms = took.elapsed();
        const QString slow = ms > 100 ? QString(" [log open %1 ms]").arg(ms) : QString();
        file.write((QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ") + text + slow + "\n").toUtf8());
        file.close();
    }
}

#endif
