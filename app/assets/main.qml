import bb.cascades 1.4
import bb.system 1.2

// The root. The tabs sit on the action bar (the user, 2026-11-14: with them in the tab menu the
// bar of a tab's first page was all but empty), and there are four, Home, Search, Queue and Now
// Playing (the user, 2026-11-15: a fourth tab, last, "if it fits"). Three is what the Q10's bar
// showed beside the action menu: with five, the ones that did not fit lived in the tab menu and
// took another's place on the bar when chosen ("i don't like the fact that the tabs shuffle
// around"); look at the fourth on every tab's first page, with and without the action menu.
// Recent and Explore open from the top of Home.
// A tab's first page has its actions in the action menu; a pushed page has the bar to itself.
// Home is first: it is the tab the app opens on. Each tab is a NavigationPane: albums, the
// queue and Now Playing are pushed on whichever is open. Search is part of this file; Home,
// Queue and Now Playing are loaded by a delegate, so a fault in one of them costs that tab and
// not the app's start.
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

    property variant lastTab: homeTab
    onActiveTabChanged: {
        if (activeTab != npTab) {
            lastTab = activeTab;
            return;
        }
        root.activeTab = lastTab;   // "root.": in this handler activeTab is the signal's argument
        var nav = lastTab == searchTab ? searchNav : (lastTab == queueTab ? queueDelegate : homeDelegate).object;
        if (nav && !(nav.top && nav.top.isNowPlaying)) {
            var p = nowPlayingDef.createObject();
            p.navPane = nav;
            nav.push(p);
        }
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

    // Not a page of its own: as a tab's first page its controls sat in the action menu. It goes
    // back to the tab that was open and pushes Now Playing there, as the strip does (the user,
    // 2026-11-15: "the same ui you have when tapping the bar, actions and all").
    Tab {
        id: npTab
        title: qsTr("Now Playing")
        imageSource: "asset:///images/ic_note.png"
    }

    onCreationCompleted: {
        // The card shown while the app is minimised, made once. A fault in it costs the card, not the app.
        var card = coverDef.createObject();
        if (card) {
            Application.cover = card;
        }
        // Every failed command and every track that could not be played: one toast, here.
        engine.reply.connect(function (id, ok, result, code, message) {
            // Not for a dropped link: the bar at the top of each page says that.
            if (!ok && code != "disconnected") {
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
            id: nowPlayingDef
            source: "NowPlayingPage.qml"
        },
        ComponentDefinition {
            id: coverDef
            source: "AppCover.qml"
        },
        ComponentDefinition {
            id: aboutSheet
            source: "AboutSheet.qml"
        }
    ]
}
