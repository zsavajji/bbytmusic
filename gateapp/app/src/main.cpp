#include <bb/cascades/AbstractPane>
#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QtDeclarative>

#include "EngineLink.h"
#include "NowPlaying.h"
#include "UiLog.h"

using namespace bb::cascades;

Q_DECL_EXPORT int main(int argc, char **argv)
{
    Application app(argc, argv);
    app.setMenuEnabled(false);
    uiLog("ui started");

    EngineLink engine(&app);
    NowPlaying nowPlaying(&app);

    // bb.cascades has no Timer: main.qml scrolls its log with this one.
    qmlRegisterType<QTimer>("gate", 1, 0, "Timer");
    QmlDocument *document = QmlDocument::create("asset:///main.qml").parent(&app);
    if (document->hasErrors()) {
        // A QML error otherwise quits the app at start with nothing to show.
        foreach (const QDeclarativeError &error, document->errors())
            uiLog("QML: " + error.toString());
        return 1;
    }
    document->setContextProperty("engine", &engine);
    document->setContextProperty("nowPlaying", &nowPlaying);
    AbstractPane *root = document->createRootObject<AbstractPane>();
    if (!root) {
        uiLog("QML: no root object");
        return 1;
    }
    app.setScene(root);
    return Application::exec();
}
