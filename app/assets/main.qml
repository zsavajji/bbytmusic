import bb.cascades 1.4
import bb.system 1.2
import bbmusic 1.0
import "quiet.js" as Quiet

// The root. The tabs sit on the action bar (the user, 2026-11-14: with them in the tab menu the
// bar of a tab's first page was all but empty), and there are four, Home, Search, Queue and My
// library: what the Q10's bar shows beside the action menu. With five, the ones that did not fit
// lived in the tab menu and took another's place on the bar when chosen ("i don't like the fact
// that the tabs shuffle around"). The fourth was Now Playing from 2026-11-15 to 2026-11-21, when
// the user had the strip open Now Playing over the whole screen instead and asked for
// the library as a tab. Recent and Explore open from the top of Home.
// A tab's first page has its actions in the action menu; a pushed page has the bar to itself.
// Home is first: it is the tab the app opens on. Each tab is a NavigationPane: albums and the
// queue are pushed on whichever is open. Search is part of this file; Home, Queue and My library
// are loaded by a delegate, so a fault in one of them costs that tab and not the app's start.
TabbedPane {
    id: root
    showTabsOnActionBar: true
    property variant dockPage       // docked mode's page while it is up (DockPage.qml)
    property bool dockAuto: false   // it opened by itself, for a display: it closes by itself too

    // Docked mode for an external display (docs/plan/docked.md). `auto`: opened for a display, so
    // the display going away closes it.
    function dock(auto) {
        if (dockPage || ! player.loaded) {
            return;
        }
        var p = dockDef.createObject(root);
        if (p) {
            p.host = root;
            root.dockPage = p;
            root.dockAuto = auto;
            // Not at once: the scene is not swapped from inside an action's handler (MiniPlayer
            // waits the same way before Now Playing takes the screen).
            dockTimer.start();
        }
    }

    function dockClosed(p) {
        if (p == dockPage) {
            dockPage = undefined;
            dockAuto = false;
        }
    }

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
                //: verb, stops playback
                title: qsTr("Stop")
                imageSource: "asset:///images/ic_stop.png"
                onTriggered: {
                    engine.request("stop", {});
                }
            },
            ActionItem {
                //: menu item, opens the page about the app
                title: qsTr("About")
                imageSource: "asset:///images/ic_info.png"
                onTriggered: {
                    var sheet = aboutSheet.createObject();
                    if (sheet) {
                        sheet.open();
                    }
                }
            }
        ]
    }

    Tab {
        id: homeTab
        title: qsTr("Home")
        imageSource: "asset:///images/ic_home.png"
        delegateActivationPolicy: TabDelegateActivationPolicy.ActivateWhenSelected
        delegate: Delegate {
            id: homeDelegate
            source: "HomeTab.qml"
            // A file that does not load leaves the tab empty: say why.
            onErrorMessageChanged: {
                root.delegateFailed(errorMessage);
            }
        }
    }

    Tab {
        id: searchTab
        title: qsTr("Search")
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
        id: queueTab
        title: qsTr("Queue")
        imageSource: "asset:///images/ic_queue.png"
        delegateActivationPolicy: TabDelegateActivationPolicy.ActivateWhenSelected
        delegate: Delegate {
            id: queueDelegate
            source: "QueueTab.qml"
            // A file that does not load leaves the tab empty: say why.
            onErrorMessageChanged: {
                root.delegateFailed(errorMessage);
            }
        }
    }

    Tab {
        id: libraryTab
        title: qsTr("My library")
        imageSource: "asset:///images/ic_library.png"
        delegateActivationPolicy: TabDelegateActivationPolicy.ActivateWhenSelected
        delegate: Delegate {
            id: libraryDelegate
            source: "LibraryTab.qml"
            // A file that does not load leaves the tab empty: say why.
            onErrorMessageChanged: {
                root.delegateFailed(errorMessage);
            }
        }
    }

    onCreationCompleted: {
        // The card shown while the app is minimised, made once. A fault in it costs the card, not the app.
        var card = coverDef.createObject();
        if (card) {
            Application.cover = card;
        }
        // Every failed command and every track that could not be played: one toast, here.
        engine.reply.connect(function (id, ok, result, code, message) {
            // Not for a dropped link: the bar at the top of each page says that. Nor for a request
            // whose page shows the failure itself (Quiet).
            var own = Quiet.take(id);
            if (!ok && code != "disconnected" && !own) {
                // "network" covers both an unreachable service and an answer the engine could
                // not read: the engine's own words say which.
                var text = code == "signed_out" ? qsTr("Sign in to YouTube first, in Settings.") : (message ? message : code);
                errorToast.body = code == "network" ? qsTr("YouTube Music: %1").arg(text) : text;
                errorToast.show();
            }
        });
        engine.event.connect(function (name, fields) {
            if (name == "favorite") {
                if (fields.on && ! fields.offline) {
                    errorToast.body = qsTr("Added to Favorites");
                    errorToast.show();
                } else if (! fields.on) {
                    errorToast.body = qsTr("Removed from Favorites");
                    errorToast.show();
                }
                return;
            }
            if (name == "skipped") {
                errorToast.body = fields.category == "music_offtopic" ? qsTr("Skipped a non-music part.") : qsTr("Skipped: %1.").arg(fields.category);
                errorToast.show();
            }
        });
        externalDisplay.attachedChanged.connect(function (on) {
            if (on) {
                root.dock(true);
            } else if (root.dockPage && root.dockAuto) {
                root.dockPage.leave();
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
        ComponentDefinition {
            id: coverDef
            source: "AppCover.qml"
        },
        ComponentDefinition {
            id: aboutSheet
            source: "AboutSheet.qml"
        },
        // Here and not on Now Playing: a page made from Now Playing's definitions loses its context
        // when Now Playing is destroyed (see MiniPlayer.qml).
        ComponentDefinition {
            id: dockDef
            source: "DockPage.qml"
        },
        Timer {
            id: dockTimer
            singleShot: true
            interval: 220
            onTimeout: {
                if (root.dockPage) {
                    root.dockPage.present();
                }
            }
        }
    ]
}
