import bb.cascades 1.4
import bb.system 1.2

// A page of shelves, as the engine's `browse` gives them: an artist (the picture and name, the
// top songs, then Albums, Singles & EPs, Videos, Featured on, Fans might also like) or a feed
// (Home: its shelves, and More while there are others). Each shelf under its header. A song plays
// with its radio; an album or a playlist opens its page; an artist opens theirs. An artist's page
// has Play (the Signature action) for the top songs.
Page {
    id: page
    property variant navPane
    property string browseId: ""
    property string browseReq: ""
    property string moreReq: ""
    property string continuation: ""
    property bool hasPlay: false
    property bool hasShare: false
    property bool isArtist: false
    property bool busy: false
    property bool failed: false
    property variant tracks: []      // the top songs

    function art(url, size, none) {
        var f = url ? thumbs.file(url, size) : "";
        return f != "" ? f : none;
    }

    function load() {
        if (browseId == "" || browseReq != "") {
            return;
        }
        browseReq = engine.request("browse", {
            "id": browseId
        });
        busy = browseReq != "";
        failed = ! busy;
    }

    function makeRow(item) {
        var playable = item.kind == "song" || item.kind == "video";
        var sub = item.subtitle ? item.subtitle : "";
        if (item.kind == "song") {
            // Under the artist's own name, the album and the plays say more than the name again.
            var parts = [];
            if (! isArtist && item.artist) {
                parts.push(item.artist);
            }
            if (item.album) {
                parts.push(item.album);
            }
            if (item.plays) {
                parts.push(item.plays);
            }
            if (parts.length > 0) {
                sub = parts.join(" • ");
            }
        }
        return {
            "rowType": playable ? "playable" : "other",
            "title": item.title,
            "subtitle": sub,
            "durationText": playable && item.duration_s > 0 ? player.fmt(item.duration_s * 1000) : "",
            "thumbUrl": item.thumb ? item.thumb : "",
            "image": art(item.thumb, 120, "asset:///images/art_none.png"),
            "item": item
        };
    }

    // The rows of a result's shelves; "More" goes last when there is a continuation.
    function sectionRows(result) {
        var rows = [];
        var sections = result.sections ? result.sections : [];
        for (var s = 0; s < sections.length; s ++) {
            var items = sections[s].items ? sections[s].items : [];
            if (items.length == 0) {
                continue;
            }
            rows.push({
                "rowType": "header",
                "title": sections[s].title
            });
            for (var k = 0; k < items.length; k ++) {
                rows.push(makeRow(items[k]));
            }
        }
        continuation = result.continuation ? result.continuation : "";
        if (continuation != "") {
            rows.push({
                "rowType": "more",
                "title": "More"
            });
        }
        return rows;
    }

    function fill(result) {
        // A local copy for the loop: reading the `tracks` property converts the whole list each time.
        var top = result.items ? result.items : [];
        tracks = top;
        isArtist = result.kind == "artist";
        var rows = [];
        if (isArtist) {
            rows.push({
                "rowType": "head",
                "title": result.title,
                "subtitle": result.subtitle ? result.subtitle : "",
                "thumbUrl": result.thumb ? result.thumb : "",
                "image": art(result.thumb, 226, "asset:///images/art_none_large.png")
            });
        }
        if (top.length > 0) {
            rows.push({
                "rowType": "header",
                "title": "Top songs"
            });
            for (var i = 0; i < top.length; i ++) {
                rows.push(makeRow(top[i]));
            }
        }
        rows = rows.concat(sectionRows(result));
        rowModel.clear();
        if (rows.length > 0) {
            rowModel.append(rows);
        }
        // ActionItem has no `visible`: Play is on the bar only when there is something to play,
        // Share in the menu only for an artist (a feed has no link worth sending).
        if (top.length > 0 && ! hasPlay) {
            page.addAction(playAction, ActionBarPlacement.Signature);
            hasPlay = true;
        }
        if (isArtist && ! hasShare) {
            page.addAction(shareAction, ActionBarPlacement.InOverflow);
            hasShare = true;
        }
    }

    function more() {
        if (moreReq != "" || continuation == "") {
            return;
        }
        moreReq = engine.request("browse_more", {
            "continuation": continuation
        });
    }

    function open(item) {
        var p;
        if (item.kind == "album" || item.kind == "playlist") {
            p = albumPage.createObject();
        } else if (item.kind == "artist") {
            p = artistPage.createObject();
        } else {
            notice.body = "Not yet";
            notice.show();
            return;
        }
        p.navPane = navPane;
        p.browseId = item.id;
        navPane.push(p);
    }

    function onReply(id, ok, result, code, message) {
        if (id == "") {
            return;
        }
        if (id == page.browseReq) {
            page.browseReq = "";
            page.busy = false;
            page.failed = ! ok;      // main.qml shows why
            if (ok) {
                page.fill(result);
            }
        } else if (id == page.moreReq) {
            page.moreReq = "";
            if (ok) {
                // The "More" row sits last; the new rows replace it.
                var last = rowModel.size() - 1;
                if (last >= 0 && rowModel.value(last).rowType == "more") {
                    rowModel.removeAt(last);
                }
                var rows = page.sectionRows(result);
                if (rows.length > 0) {
                    rowModel.append(rows);
                }
            }
        }
    }

    function onThumb(url, size, file) {
        for (var i = 0; i < rowModel.size(); i ++) {
            var row = rowModel.value(i);
            if (row.thumbUrl == url && row.image != file && size == (row.rowType == "head" ? 226 : 120)) {
                row.image = file;
                rowModel.replace(i, row);
            }
        }
    }

    // The engine is back (or was not there yet when the page was made): try again.
    function onLink() {
        if (engine.connected && page.failed) {
            page.load();
        }
    }

    // Called by main.qml when the page has been popped.
    // One try each: a disconnect that throws must not leave the others connected.
    function cleanup() {
        try {
            engine.reply.disconnect(page.onReply);
        } catch (e0) {
        }
        try {
            engine.connectedChanged.disconnect(page.onLink);
        } catch (e9) {
        }
        try {
            thumbs.ready.disconnect(page.onThumb);
        } catch (e1) {
        }
    }

    onBrowseIdChanged: {
        load();
    }

    onCreationCompleted: {
        engine.reply.connect(page.onReply);
        engine.connectedChanged.connect(page.onLink);
        thumbs.ready.connect(page.onThumb);
        load();
    }

    Container {
        layout: StackLayout {
        }

        ConnectionBar {
        }
        ActivityIndicator {
            running: page.busy
            visible: page.busy
            horizontalAlignment: HorizontalAlignment.Center
            topMargin: ui.du(10)
            preferredWidth: ui.du(12)
            preferredHeight: ui.du(12)
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
                    page.load();
                }
            }
        }

        ListView {
            id: list
            visible: ! page.busy && ! page.failed
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            dataModel: ArrayDataModel {
                id: rowModel
            }

            function itemType(data, indexPath) {
                return data.rowType;
            }
            // Called by the rows' context actions (list items cannot see this page's ids).
            function queueAdd(indexPath, next) {
                engine.request("queue_add", {
                    "items": [ dataModel.data(indexPath).item ],
                    "next": next
                });
            }

            listItemComponents: [
                ListItemComponent {
                    type: "head"
                    Container {
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        leftPadding: ui.du(2)
                        rightPadding: ui.du(2)
                        topPadding: ui.du(2)
                        bottomPadding: ui.du(2)
                        ImageView {
                            imageSource: ListItemData.image
                            preferredWidth: ui.du(24)
                            preferredHeight: ui.du(24)
                            minWidth: ui.du(24)
                            scalingMethod: ScalingMethod.AspectFill
                            loadEffect: ImageViewLoadEffect.None
                            verticalAlignment: VerticalAlignment.Center
                        }
                        Container {
                            leftMargin: ui.du(2)
                            verticalAlignment: VerticalAlignment.Center
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                            Label {
                                text: ListItemData.title
                                multiline: true
                                autoSize.maxLineCount: 3
                                textStyle.fontSize: FontSize.Large
                                textStyle.color: Color.White
                                bottomMargin: 0
                            }
                            Label {
                                text: ListItemData.subtitle
                                visible: ListItemData.subtitle != ""
                                textStyle.fontSize: FontSize.Small
                                textStyle.color: Color.create("#969696")
                                topMargin: ui.du(1)
                            }
                        }
                    }
                },
                ListItemComponent {
                    type: "header"
                    Header {
                        title: ListItemData.title
                    }
                },
                ListItemComponent {
                    type: "playable"
                    StandardListItem {
                        id: song
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
                                        song.ListItem.view.queueAdd(song.ListItem.indexPath, true);
                                    }
                                }
                                ActionItem {
                                    title: "Add to Queue"
                                    imageSource: "asset:///images/ic_add_queue.png"
                                    onTriggered: {
                                        song.ListItem.view.queueAdd(song.ListItem.indexPath, false);
                                    }
                                }
                            }
                        ]
                    }
                },
                ListItemComponent {
                    type: "other"
                    StandardListItem {
                        title: ListItemData.title
                        description: ListItemData.subtitle
                        imageSource: ListItemData.image
                    }
                },
                ListItemComponent {
                    type: "more"
                    StandardListItem {
                        title: ListItemData.title
                    }
                }
            ]

            onTriggered: {
                var entry = dataModel.data(indexPath);
                if (entry.rowType == "playable") {
                    engine.request("play", {
                        "items": [ entry.item ],
                        "index": 0,
                        "radio": true
                    });
                } else if (entry.rowType == "other") {
                    page.open(entry.item);
                } else if (entry.rowType == "more") {
                    page.more();
                }
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
        },
        ComponentDefinition {
            id: artistPage
            source: "ShelfPage.qml"
        },
        InvokeActionItem {
            id: shareAction
            query {
                mimeType: "text/plain"
                invokeActionId: "bb.action.SHARE"
            }
            onTriggered: {
                data = "https://music.youtube.com/channel/" + page.browseId;
            }
        },
        ActionItem {
            id: playAction
            title: "Play"
            imageSource: "asset:///images/ic_play.png"
            onTriggered: {
                engine.request("play", {
                    "items": page.tracks,
                    "index": 0,
                    "radio": false
                });
            }
        }
    ]
}
