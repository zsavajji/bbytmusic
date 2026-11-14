#ifndef UILOG_H
#define UILOG_H

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QString>

// Appends a timestamped line to gate-ui.log in the app's data folder, so the UI's side (what the
// lock screen sent, what the engine said) can be read after the app is gone.
inline void uiLog(const QString &text)
{
    QFile file(QDir::homePath() + "/gate-ui.log");
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        file.write((QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ") + text + "\n").toUtf8());
        file.close();
    }
}

#endif
