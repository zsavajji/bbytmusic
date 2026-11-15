import bb.cascades 1.4
import bb.system 1.2

// What was listened to, the latest first, from the engine's own history (`history`): the
// "Recently Played" of YTMusicWP's library, with no account needed. Pushed from the top of Home.
// A tap plays the track with its radio; Play All plays the list as it stands.
Page {
    id: page
    property variant navPane
    property variant tracks: []
    property string historyReq: ""
    property bool asked: false
    property bool failed: false

    function art(url) {
        var f = url ? thumbs.file(url, 120) : "";
        return f != "" ? f : "asset:///images/art_none.png";
    }

    function refresh() {
        if (historyReq == "") {
            historyReq = engine.request("history", {
                "limit": 100
            });
            failed = historyReq == "";
        }
    }

    function fill(result) {
        // A local copy for the loop: reading the `tracks` property converts the whole list each time.
        var items = result.items ? result.items : [];
        tracks = items;
        var rows = [];
        for (var i = 0; i < items.length; i ++) {
            var t = items[i];
            var sub = t.artist ? t.artist : "";
            if (t.album) {
                sub = sub != "" ? sub + " • " + t.album : t.album;
            }
            rows.push({
                "title": t.title,
                "subtitle": sub,
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
            notice.body = "This track has no album page.";
            notice.show();
            return;
        }
        var p = albumPage.createObject();
        p.navPane = navPane;
        p.browseId = id;
        navPane.push(p);
    }

    function onReply(id, ok, result, code, message) {
        if (id != "" && id == page.historyReq) {
            page.historyReq = "";
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
            title: "Play All"
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
            title: "Recently played"
            subtitle: page.tracks.length == 1 ? "1 track" : page.tracks.length + " tracks"
        }
        Label {
            text: "Nothing yet. What you listen to shows up here."
            multiline: true
            visible: page.asked && page.tracks.length == 0 && ! page.failed
            horizontalAlignment: HorizontalAlignment.Center
        }
        Container {
            visible: page.failed
            horizontalAlignment: HorizontalAlignment.Center
            topPadding: ui.du(6)
            Label {
                text: "This page could not be loaded."
                horizontalAlignment: HorizontalAlignment.Center
            }
            Button {
                text: "Retry"
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
                                title: "Play Next"
                                imageSource: "asset:///images/ic_play_next.png"
                                onTriggered: {
                                    row.ListItem.view.queueAdd(row.ListItem.indexPath[0], true);
                                }
                            }
                            ActionItem {
                                title: "Add to Queue"
                                imageSource: "asset:///images/ic_add_queue.png"
                                onTriggered: {
                                    row.ListItem.view.queueAdd(row.ListItem.indexPath[0], false);
                                }
                            }
                            ActionItem {
                                title: "View Album"
                                imageSource: "asset:///images/ic_album.png"
                                onTriggered: {
                                    row.ListItem.view.viewAlbum(row.ListItem.indexPath[0]);
                                }
                            }
                        }
                    ]
                }
            }

            onTriggered: {
                engine.request("play", {
                    "items": [ page.tracks[indexPath[0]] ],
                    "index": 0,
                    "radio": true
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
