import bb.cascades 1.2
import bb.system 1.2

// The root: Search and Queue, each a NavigationPane (Now Playing is pushed on whichever is open).
TabbedPane {
    id: root
    showTabsOnActionBar: true

    Menu.definition: MenuDefinition {
        actions: [
            ActionItem {
                title: "Stop"
                onTriggered: {
                    engine.request("stop", {});
                }
            },
            ActionItem {
                title: "About"
                onTriggered: {
                    about.show();
                }
            }
        ]
    }

    Tab {
        title: "Search"
        // Back on this tab: type straight away.
        onTriggered: {
            searchPage.focusField();
        }
        NavigationPane {
            id: searchNav
            SearchPage {
                id: searchPage
                navPane: searchNav
            }
            onPopTransitionEnded: {
                page.destroy();
            }
        }
    }

    Tab {
        title: "Queue"
        NavigationPane {
            id: queueNav
            QueuePage {
                navPane: queueNav
            }
            onPopTransitionEnded: {
                page.destroy();
            }
        }
    }

    onCreationCompleted: {
        // Every failed command and every track that could not be played: one toast, here.
        engine.reply.connect(function (id, ok, result, code, message) {
            if (!ok) {
                errorToast.body = message ? message : code;
                errorToast.show();
            }
        });
        player.errorOccurred.connect(function (message) {
            errorToast.body = message;
            errorToast.show();
        });
    }

    attachedObjects: [
        SystemToast {
            id: errorToast
            autoUpdateEnabled: true
        },
        SystemDialog {
            id: about
            title: "BBMusic"
            body: "A YouTube Music client.\nVersion 0.1.0"
            confirmButton.label: "OK"
            cancelButton.label: ""
        }
    ]
}
