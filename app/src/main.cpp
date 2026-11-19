#include <bb/cascades/AbstractPane>
#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <QDir>
#include <QLocale>
#include <QFile>
#include <QStringList>
#include <QTimer>
#include <QTranslator>
#include <QtDeclarative>

#include "EngineLink.h"
#include "PlayerState.h"
#include "Thumbs.h"
#include "UiLog.h"

using namespace bb::cascades;

// When data/check-qml exists (put there by hand over ssh), every QML document of the app is
// compiled at start and what the compiler says goes to bbmusic-ui.log: "check: ok X.qml" or the
// errors. Most pages are created only when first opened, so without this a fault in one shows
// only as a page that never appears. It finds what the compiler finds (unknown types,
// properties, signal handlers, enum values, JavaScript that does not parse), not what a binding
// does when it runs. It needs the running app: QmlDocument aborts without a
// bb::cascades::Application, and that cannot be built from a shell (both tried, 2026-11-14).
static void checkDocuments(Application &app)
{
    if (!QFile::exists(QDir::homePath() + "/check-qml"))
        return;
    const QStringList names = QDir("app/native/assets").entryList(QStringList("*.qml"), QDir::Files, QDir::Name);
    int failed = 0;
    foreach (const QString &name, names) {
        QmlDocument *document = QmlDocument::create("asset:///" + name).parent(&app);
        if (!document->hasErrors()) {
            uiLog("check: ok " + name);
        } else {
            ++failed;
            foreach (const QDeclarativeError &error, document->errors())
                uiLog("check: FAIL " + name + ": " + error.toString());
        }
        document->deleteLater();
    }
    uiLog(QString("check: %1 of %2 documents failed").arg(failed).arg(names.size()));
}

Q_DECL_EXPORT int main(int argc, char **argv)
{
    Application app(argc, argv);
    uiLog("ui started");

    // ponytail: the language is read once at start; a change in the system's settings shows on
    // the next launch (bb::cascades::LocaleHandler would retranslate live).
    QTranslator translator;
    if (translator.load("bbmusic_" + QLocale().name(), "app/native/qm"))
        app.installTranslator(&translator);

    EngineLink engine(&app);
    PlayerState player(&app);
    Thumbs thumbs(&engine, &app);
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
    document->setContextProperty("thumbs", &thumbs);
    AbstractPane *root = document->createRootObject<AbstractPane>();
    if (!root) {
        uiLog("QML: no root object");
        return 1;
    }
    app.setScene(root);
    checkDocuments(app);
    engine.start();
    return Application::exec();
}
