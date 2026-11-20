import bb.cascades 1.4
import bb.system 1.2

// The favorites, the latest added first, from the engine's own store (`favorites`): kept on the
// phone and played offline, no account needed. Pushed from the Library page.
// A tap plays the list from that row (no radio: it must work without the network).
Page {
    id: page
    property variant navPane
    property variant tracks: []
    property string favReq: ""
    property bool asked: false
    property bool failed: false
    // The engine is still waking, not down: the spinner, not the error.
    property bool waking: failed && ! engine.connected && ! engine.down

    function art(url) {
        var f = url ? thumbs.file(url, 120) : "";
        return f != "" ? f : "asset:///images/art_none.png";
    }

    function refresh() {
        if (favReq == "") {
            favReq = engine.request("favorites", {});
            failed = favReq == "";
        }
    }

    function subtitleOf(t) {
        var sub = t.artist ? t.artist : "";
        if (t.album) {
            sub = sub != "" ? sub + " \u2022 " + t.album : t.album;
        }
        if (! t.offline) {
            sub = sub != "" ? sub + " \u2022 " + qsTr("not downloaded yet") : qsTr("not downloaded yet");
        }
        return sub;
    }

    function fill(result) {
        // A local copy for the loop: reading the `tracks` property converts the whole list each time.
        var items = result.items ? result.items : [];
        tracks = items;
        var rows = [];
        for (var i = 0; i < items.length; i ++) {
            var t = items[i];
            rows.push({
                "title": t.title,
                "subtitle": subtitleOf(t),
                "durationText": t.duration_s > 0 ? player.fmt(t.duration_s * 1000) : "",
                "thumbUrl": t.thumb ? t.thumb : "",
                "image": art(t.thumb)
            });
        }
        rowModel.clear();
        if (rows.length > 0) {
            rowModel.append(rows);
        }
        asked = true;
    }

    function viewAlbum(index) {
        var id = tracks[index].album_id;
        if (! id) {
            notice.body = qsTr("This track has no album page.");
            notice.show();
            return;
        }
        var p = albumPage.createObject();
        p.navPane = navPane;
        p.browseId = id;
        navPane.push(p);
    }

    function shuffled() {
        var copy = tracks;      // reading the property gives a fresh array
        for (var k = copy.length - 1; k > 0; k --) {
            var j = Math.floor(Math.random() * (k + 1));
            var t = copy[k];
            copy[k] = copy[j];
            copy[j] = t;
        }
        return copy;
    }

    function unfavorite(index) {
        engine.request("favorite", {
            "id": tracks[index].id,
            "on": false
        });
    }

    // A favorite removed is dropped in place (a reload would make the list jump); a download
    // that completed changes its row's subtitle; a new one is not in the list yet: read it again.
    function onFavorite(fields) {
        var copy = tracks;
        var at = -1;
        for (var i = 0; i < copy.length; i ++) {
            if (copy[i].id == fields.id) {
                at = i;
                break;
            }
        }
        if (! fields.on) {
            if (at >= 0) {
                copy.splice(at, 1);
                tracks = copy;
                rowModel.removeAt(at);
            }
        } else if (at >= 0) {
            copy[at].offline = fields.offline;
            tracks = copy;
            var row = rowModel.value(at);
            row.subtitle = subtitleOf(copy[at]);
            rowModel.replace(at, row);
        } else {
            refresh();
        }
    }

    function onReply(id, ok, result, code, message) {
        if (id != "" && id == page.favReq) {
            page.favReq = "";
            page.failed = ! ok;      // main.qml shows why
            if (ok) {
                page.fill(result);
            }
        }
    }

    // The list is read when the page is made, not at every track: it would jump back to its top
    // under a finger. And once the engine is back after a drop, which also retries a failed read.
    function onEvent(name, fields) {
        if (name == "status") {
            page.refresh();
        } else if (name == "favorite") {
            page.onFavorite(fields);
        }
    }

    function onThumb(url, size, file) {
        if (size != 120) {
            return;
        }
        for (var i = 0; i < rowModel.size(); i ++) {
            var row = rowModel.value(i);
            if (row.thumbUrl == url && row.image != file) {
                row.image = file;
                rowModel.replace(i, row);
            }
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
        try {
            thumbs.ready.disconnect(page.onThumb);
        } catch (e2) {
        }
    }

    onCreationCompleted: {
        engine.reply.connect(page.onReply);
        engine.event.connect(page.onEvent);
        thumbs.ready.connect(page.onThumb);
        page.refresh();
    }

    // Letter shortcuts are safe here: no text field on this page.
    actions: [
        ActionItem {
            title: qsTr("Shuffle")
            imageSource: "asset:///images/ic_shuffle.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: {
                if (page.tracks.length > 0) {
                    engine.request("play", {
                        "items": page.shuffled(),
                        "index": 0,
                        "radio": false
                    });
                }
            }
        },
        ActionItem {
            //: plays every song of the list
            title: qsTr("Play All")
            imageSource: "asset:///images/ic_play.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: {
                if (page.tracks.length > 0) {
                    engine.request("play", {
                        "items": page.tracks,
                        "index": 0,
                        "radio": false
                    });
                }
            }
        }
    ]

    Container {
        layout: StackLayout {
        }

        ConnectionBar {
        }
        Header {
            title: qsTr("Favorites")
            subtitle: qsTr("%n track(s)", "", page.tracks.length)
        }
        Label {
            text: qsTr("No favorites yet. Add a song from its menu: it is kept on the phone and plays offline.")
            multiline: true
            visible: page.asked && page.tracks.length == 0 && ! page.failed
            horizontalAlignment: HorizontalAlignment.Center
        }
        ActivityIndicator {
            running: page.waking
            visible: page.waking
            horizontalAlignment: HorizontalAlignment.Center
            preferredWidth: ui.du(8)
            preferredHeight: ui.du(8)
        }
        Container {
            visible: page.failed && ! page.waking
            horizontalAlignment: HorizontalAlignment.Center
            topPadding: ui.du(6)
            Label {
                text: qsTr("This page could not be loaded.")
                horizontalAlignment: HorizontalAlignment.Center
            }
            Button {
                text: qsTr("Retry")
                horizontalAlignment: HorizontalAlignment.Center
                onClicked: {
                    page.refresh();
                }
            }
        }
        ListView {
            id: list
            visible: ! page.failed
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            dataModel: ArrayDataModel {
                id: rowModel
            }

            // Called by the rows' context actions (list items cannot see this page's ids).
            function queueAdd(index, next) {
                engine.request("queue_add", {
                    "items": [ page.tracks[index] ],
                    "next": next
                });
            }
            function viewAlbum(index) {
                page.viewAlbum(index);
            }
            function unfavorite(index) {
                page.unfavorite(index);
            }

            listItemComponents: ListItemComponent {
                StandardListItem {
                    id: row
                    title: ListItemData.title
                    description: ListItemData.subtitle
                    status: ListItemData.durationText
                    imageSource: ListItemData.image
                    contextActions: [
                        ActionSet {
                            title: ListItemData.title
                            subtitle: ListItemData.subtitle
                            ActionItem {
                                //: puts the song right after the one playing
                                title: qsTr("Play Next")
                                imageSource: "asset:///images/ic_play_next.png"
                                onTriggered: {
                                    row.ListItem.view.queueAdd(row.ListItem.indexPath[0], true);
                                }
                            }
                            ActionItem {
                                title: qsTr("Add to Queue")
                                imageSource: "asset:///images/ic_add_queue.png"
                                onTriggered: {
                                    row.ListItem.view.queueAdd(row.ListItem.indexPath[0], false);
                                }
                            }
                            ActionItem {
                                title: qsTr("View Album")
                                imageSource: "asset:///images/ic_album.png"
                                onTriggered: {
                                    row.ListItem.view.viewAlbum(row.ListItem.indexPath[0]);
                                }
                            }
                            ActionItem {
                                title: qsTr("Remove from Favorites")
                                imageSource: "asset:///images/ic_clear.png"
                                onTriggered: {
                                    row.ListItem.view.unfavorite(row.ListItem.indexPath[0]);
                                }
                            }
                        }
                    ]
                }
            }

            onTriggered: {
                engine.request("play", {
                    "items": page.tracks,
                    "index": indexPath[0],
                    "radio": false
                });
            }
        }

        MiniPlayer {
            navPane: page.navPane
        }
    }

    attachedObjects: [
        SystemToast {
            id: notice
            autoUpdateEnabled: true
        },
        ComponentDefinition {
            id: albumPage
            source: "AlbumPage.qml"
        }
    ]
}
