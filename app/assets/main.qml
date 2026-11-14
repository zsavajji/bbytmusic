import bb.cascades 1.4
import bb.system 1.2

// The root. The tabs sit on the action bar (the user, 2026-11-14: with them in the tab menu the
// bar of a tab's first page was all but empty), and there are three, Home, Search and Queue,
// because three is what the Q10's bar shows beside the action menu: with five, the ones that
// did not fit lived in the tab menu and took another's place on the bar when chosen ("i don't
// like the fact that the tabs shuffle around"). Recent and Explore open from the top of Home.
// A tab's first page has its actions in the action menu; a pushed page has the bar to itself.
// Home is first: it is the tab the app opens on. Each tab is a NavigationPane: albums, the
// queue and Now Playing are pushed on whichever is open. Search is part of this file; Home and
// Queue are loaded by a delegate, so a fault in one of them costs that tab and not the app's
// start.
TabbedPane {
    id: root
    showTabsOnActionBar: true

    // A page that was pushed is done with: let it drop its signal connections, then free it.
    function retire(page) {
        if (page.cleanup) {
            page.cleanup();
        }
        page.destroy();
    }

    function delegateFailed(message) {
        if (message != "") {
            errorToast.body = message;
            errorToast.show();
        }
    }

    Menu.definition: MenuDefinition {
        settingsAction: SettingsActionItem {
            onTriggered: {
                var sheet = settingsSheet.createObject();
                if (sheet) {
                    sheet.show();
                }
            }
        }
        actions: [
            ActionItem {
                title: "Stop"
                imageSource: "asset:///images/ic_stop.png"
                onTriggered: {
                    engine.request("stop", {});
                }
            },
            ActionItem {
                title: "About"
                imageSource: "asset:///images/ic_info.png"
                onTriggered: {
                    about.show();
                }
            }
        ]
    }

    Tab {
        title: "Home"
        imageSource: "asset:///images/ic_home.png"
        delegateActivationPolicy: TabDelegateActivationPolicy.ActivateWhenSelected
        delegate: Delegate {
            source: "HomeTab.qml"
            // A file that does not load leaves the tab empty: say why.
            onErrorMessageChanged: {
                root.delegateFailed(errorMessage);
            }
        }
    }

    Tab {
        title: "Search"
        imageSource: "asset:///images/ic_search.png"
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
                root.retire(page);
            }
        }
    }

    Tab {
        title: "Queue"
        imageSource: "asset:///images/ic_queue.png"
        delegateActivationPolicy: TabDelegateActivationPolicy.ActivateWhenSelected
        delegate: Delegate {
            source: "QueueTab.qml"
            // A file that does not load leaves the tab empty: say why.
            onErrorMessageChanged: {
                root.delegateFailed(errorMessage);
            }
        }
    }

    onCreationCompleted: {
        // Every failed command and every track that could not be played: one toast, here.
        engine.reply.connect(function (id, ok, result, code, message) {
            // Not for a dropped link: the bar at the top of each page says that.
            if (!ok && code != "disconnected") {
                // "network" covers both an unreachable service and an answer the engine could
                // not read: the engine's own words say which.
                errorToast.body = (code == "network" ? "YouTube Music: " : "") + (message ? message : code);
                errorToast.show();
            }
        });
        engine.event.connect(function (name, fields) {
            if (name == "skipped") {
                errorToast.body = fields.category == "music_offtopic" ? "Skipped a non-music part." : "Skipped: " + fields.category + ".";
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
        ComponentDefinition {
            id: settingsSheet
            source: "SettingsSheet.qml"
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
