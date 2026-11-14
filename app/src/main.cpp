#include <bb/cascades/AbstractPane>
#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <QTimer>
#include <QtDeclarative>

#include "EngineLink.h"
#include "PlayerState.h"
#include "UiLog.h"

using namespace bb::cascades;

Q_DECL_EXPORT int main(int argc, char **argv)
{
    Application app(argc, argv);
    uiLog("ui started");

    EngineLink engine(&app);
    PlayerState player(&app);
    QObject::connect(&engine, SIGNAL(event(QString,QVariantMap)),
                     &player, SLOT(onEvent(QString,QVariantMap)));

    // bb.cascades has no Timer: the search page's debounce and focus use this one.
    qmlRegisterType<QTimer>("bbmusic", 1, 0, "Timer");
    QmlDocument *document = QmlDocument::create("asset:///main.qml").parent(&app);
    if (document->hasErrors()) {
        // A QML error otherwise quits the app at start with nothing to show.
        foreach (const QDeclarativeError &error, document->errors())
            uiLog("QML: " + error.toString());
        return 1;
    }
    document->setContextProperty("engine", &engine);
    document->setContextProperty("player", &player);
    AbstractPane *root = document->createRootObject<AbstractPane>();
    if (!root) {
        uiLog("QML: no root object");
        return 1;
    }
    app.setScene(root);
    engine.start();
    return Application::exec();
}
