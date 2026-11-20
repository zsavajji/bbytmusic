import bb.cascades 1.4

// The library: the favorites kept on the phone, and, once signed in to YouTube, the account's
// playlists, liked songs, albums and artists. Pushed from the top of Home. Signed out, the one
// link under the account's header opens Settings, where the sign-in is.
Page {
    id: page
    property variant navPane
    property bool signedIn: false
    property string accountReq: ""

    function link(title, subtitle, image, target) {
        return {
            "rowType": "link",
            "title": title,
            "subtitle": subtitle,
            "image": "asset:///images/" + image,
            "target": target
        };
    }

    function build() {
        var rows = [];
        rows.push(link(qsTr("Favorites"), qsTr("Kept on the phone, play offline"), "ic_favorite.png", "favorites"));
        rows.push({
            "rowType": "header",
            "title": qsTr("YouTube account")
        });
        if (signedIn) {
            rows.push(link(qsTr("Liked songs"), "", "ic_like.png", "VLLM"));
            rows.push(link(qsTr("Playlists"), "", "ic_queue.png", "FEmusic_liked_playlists"));
            rows.push(link(qsTr("Albums"), "", "ic_album.png", "FEmusic_liked_albums"));
            rows.push(link(qsTr("Artists"), "", "ic_artist.png", "FEmusic_library_corpus_artists"));
            rows.push(link(qsTr("Recent activity"), qsTr("What you played on YouTube"), "ic_recent.png", "FEmusic_last_played"));
        } else {
            rows.push(link(qsTr("Sign in to YouTube"), qsTr("Your playlists, liked songs and albums"), "ic_artist.png", "signin"));
        }
        rowModel.clear();
        rowModel.append(rows);
    }

    function askAccount() {
        if (accountReq == "") {
            accountReq = engine.request("account", {});
        }
    }

    function setSignedIn(state) {
        var now = state == "in";
        if (now != signedIn) {
            signedIn = now;
            build();
        }
    }

    function openLink(target) {
        var p;
        if (target == "signin") {
            var sheet = settingsSheet.createObject();
            if (sheet) {
                sheet.show();
            }
            return;
        }
        if (target == "favorites") {
            p = favoritesPage.createObject();
        } else if (target == "VLLM") {
            p = albumPage.createObject();
        } else {
            p = shelfPage.createObject();
        }
        p.navPane = navPane;
        if (target != "favorites") {
            p.browseId = target;
        }
        navPane.push(p);
    }

    function onReply(id, ok, result, code, message) {
        if (id != "" && id == page.accountReq) {
            page.accountReq = "";
            if (ok) {
                page.setSignedIn(result.state);
            }
        }
    }

    function onEvent(name, fields) {
        if (name == "account") {
            page.setSignedIn(fields.state);
        } else if (name == "status") {
            // The engine is back: ask again.
            page.askAccount();
        }
    }

    // Called by the NavigationPane when the page has been popped. One try each: a disconnect
    // that throws must not leave the others connected.
    function cleanup() {
        try {
            engine.reply.disconnect(page.onReply);
        } catch (e0) {
        }
        try {
            engine.event.disconnect(page.onEvent);
        } catch (e1) {
        }
    }

    onCreationCompleted: {
        engine.reply.connect(page.onReply);
        engine.event.connect(page.onEvent);
        page.build();
        page.askAccount();
    }

    Container {
        layout: StackLayout {
        }

        ConnectionBar {
        }
        Header {
            title: qsTr("Library")
        }
        ListView {
            id: list
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            dataModel: ArrayDataModel {
                id: rowModel
            }

            function itemType(data, indexPath) {
                return data.rowType;
            }

            listItemComponents: [
                ListItemComponent {
                    type: "header"
                    Header {
                        title: ListItemData.title
                    }
                },
                ListItemComponent {
                    type: "link"
                    StandardListItem {
                        title: ListItemData.title
                        description: ListItemData.subtitle
                        imageSource: ListItemData.image
                    }
                }
            ]

            onTriggered: {
                var entry = dataModel.data(indexPath);
                if (entry.rowType == "link") {
                    page.openLink(entry.target);
                }
            }
        }

        MiniPlayer {
            navPane: page.navPane
        }
    }

    attachedObjects: [
        ComponentDefinition {
            id: favoritesPage
            source: "FavoritesPage.qml"
        },
        ComponentDefinition {
            id: albumPage
            source: "AlbumPage.qml"
        },
        ComponentDefinition {
            id: shelfPage
            source: "ShelfPage.qml"
        },
        ComponentDefinition {
            id: settingsSheet
            source: "SettingsSheet.qml"
        }
    ]
}
