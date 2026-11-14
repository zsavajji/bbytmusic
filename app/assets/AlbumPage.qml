import bb.cascades 1.4
import bb.system 1.2

// An album's page (or a playlist's: the engine's `browse` answers both alike). The cover with the
// title, artist, year and length scrolls away with the list; under it the tracks. A tap plays the
// album from that track; Play (the Signature action) from the first; Shuffle in a random order.
Page {
    id: page
    property variant navPane
    property string browseId: ""
    property string browseReq: ""
    property bool busy: false
    property bool failed: false
    property bool isAlbum: true
    property variant info            // the browse result
    property variant tracks: []      // its items

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

    function fill(result) {
        info = result;
        isAlbum = result.kind == "album";
        tracks = result.items ? result.items : [];
        var rows = [ {
                "rowType": "head",
                "title": result.title,
                "artist": result.artist ? result.artist : "",
                "subtitle": result.subtitle ? result.subtitle : "",
                "second": result.second_subtitle ? result.second_subtitle : "",
                "thumbUrl": result.thumb ? result.thumb : "",
                "image": art(result.thumb, 226, "asset:///images/art_none_large.png")
            } ];
        for (var i = 0; i < tracks.length; i ++) {
            var t = tracks[i];
            var sub;
            if (isAlbum) {
                // The album's artist is in the header: a row names its own only when it differs.
                sub = t.artist && t.artist != result.artist ? t.artist : "";
                if (t.plays) {
                    sub = sub != "" ? sub + " • " + t.plays : t.plays;
                }
            } else {
                sub = t.artist ? t.artist : "";
                if (t.album) {
                    sub = sub != "" ? sub + " • " + t.album : t.album;
                }
            }
            rows.push({
                "rowType": isAlbum ? "track" : "ptrack",
                "title": isAlbum ? (i + 1) + ".  " + t.title : t.title,
                "subtitle": sub,
                "durationText": t.duration_s > 0 ? player.fmt(t.duration_s * 1000) : "",
                "thumbUrl": ! isAlbum && t.thumb ? t.thumb : "",
                "image": isAlbum ? "" : art(t.thumb, 120, "asset:///images/art_none.png")
            });
        }
        trackModel.clear();
        trackModel.append(rows);
    }

    function play(index) {
        if (tracks.length > 0) {
            engine.request("play", {
                "items": tracks,
                "index": index,
                "radio": false
            });
        }
    }

    function shuffled() {
        var copy = [];
        for (var i = 0; i < tracks.length; i ++) {
            copy.push(tracks[i]);
        }
        for (var k = copy.length - 1; k > 0; k --) {
            var j = Math.floor(Math.random() * (k + 1));
            var t = copy[k];
            copy[k] = copy[j];
            copy[j] = t;
        }
        return copy;
    }

    function queueAdd(items, next) {
        if (items.length > 0) {
            engine.request("queue_add", {
                "items": items,
                "next": next
            });
            notice.body = next ? "Plays next." : "Added to the queue.";
            notice.show();
        }
    }

    function onReply(id, ok, result, code, message) {
        if (id == "" || id != page.browseReq) {
            return;
        }
        page.browseReq = "";
        page.busy = false;
        page.failed = ! ok;      // main.qml shows why
        if (ok) {
            page.fill(result);
        }
    }

    function onThumb(url, size, file) {
        for (var i = 0; i < trackModel.size(); i ++) {
            var row = trackModel.value(i);
            if (row.thumbUrl == url && row.image != file && size == (row.rowType == "head" ? 226 : 120)) {
                row.image = file;
                trackModel.replace(i, row);
            }
        }
    }

    // Called by main.qml when the page has been popped.
    function cleanup() {
        try {
            engine.reply.disconnect(page.onReply);
            thumbs.ready.disconnect(page.onThumb);
        } catch (e) {
        }
    }

    onBrowseIdChanged: {
        load();
    }

    onCreationCompleted: {
        engine.reply.connect(page.onReply);
        thumbs.ready.connect(page.onThumb);
        load();
    }

    // Letter shortcuts are safe here: no text field on this page.
    actions: [
        ActionItem {
            title: "Shuffle"
            imageSource: "asset:///images/ic_shuffle.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            shortcuts: Shortcut {
                key: "S"
            }
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
            title: "Play"
            imageSource: "asset:///images/ic_play.png"
            ActionBar.placement: ActionBarPlacement.Signature
            onTriggered: {
                page.play(0);
            }
        },
        ActionItem {
            title: "Play Next"
            imageSource: "asset:///images/ic_play_next.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                page.queueAdd(page.tracks, true);
            }
        },
        ActionItem {
            title: "Add to Queue"
            imageSource: "asset:///images/ic_add_queue.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                page.queueAdd(page.tracks, false);
            }
        },
        InvokeActionItem {
            ActionBar.placement: ActionBarPlacement.InOverflow
            query {
                mimeType: "text/plain"
                invokeActionId: "bb.action.SHARE"
            }
            onTriggered: {
                data = page.info && page.info.playlist_id ? "https://music.youtube.com/playlist?list=" + page.info.playlist_id : "https://music.youtube.com/browse/" + page.browseId;
            }
        }
    ]

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
                id: trackModel
            }

            function itemType(data, indexPath) {
                return data.rowType;
            }
            // Called by the rows' context actions (list items cannot see this page's ids). Row 0
            // is the header: track n is row n + 1.
            function queueAdd(indexPath, next) {
                page.queueAdd([ page.tracks[indexPath[0] - 1] ], next);
            }
            function radio(indexPath) {
                engine.request("play", {
                    "items": [ page.tracks[indexPath[0] - 1] ],
                    "index": 0,
                    "radio": true
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
                                text: ListItemData.artist
                                visible: ListItemData.artist != ""
                                textStyle.fontSize: FontSize.Medium
                                textStyle.color: Color.White
                                topMargin: 0
                                bottomMargin: 0
                            }
                            Label {
                                text: ListItemData.subtitle
                                visible: ListItemData.subtitle != ""
                                textStyle.fontSize: FontSize.Small
                                textStyle.color: Color.create("#969696")
                                topMargin: ui.du(1)
                                bottomMargin: 0
                            }
                            Label {
                                text: ListItemData.second
                                visible: ListItemData.second != ""
                                textStyle.fontSize: FontSize.Small
                                textStyle.color: Color.create("#969696")
                                topMargin: 0
                            }
                        }
                    }
                },
                ListItemComponent {
                    type: "track"
                    StandardListItem {
                        id: track
                        title: ListItemData.title
                        description: ListItemData.subtitle
                        status: ListItemData.durationText
                        imageSpaceReserved: false
                        contextActions: [
                            ActionSet {
                                title: ListItemData.title
                                ActionItem {
                                    title: "Play Next"
                                    imageSource: "asset:///images/ic_play_next.png"
                                    onTriggered: {
                                        track.ListItem.view.queueAdd(track.ListItem.indexPath, true);
                                    }
                                }
                                ActionItem {
                                    title: "Add to Queue"
                                    imageSource: "asset:///images/ic_add_queue.png"
                                    onTriggered: {
                                        track.ListItem.view.queueAdd(track.ListItem.indexPath, false);
                                    }
                                }
                                ActionItem {
                                    title: "Start Radio"
                                    imageSource: "asset:///images/ic_radio.png"
                                    onTriggered: {
                                        track.ListItem.view.radio(track.ListItem.indexPath);
                                    }
                                }
                            }
                        ]
                    }
                },
                ListItemComponent {
                    type: "ptrack"
                    StandardListItem {
                        id: ptrack
                        title: ListItemData.title
                        description: ListItemData.subtitle
                        status: ListItemData.durationText
                        imageSource: ListItemData.image
                        contextActions: [
                            ActionSet {
                                title: ListItemData.title
                                ActionItem {
                                    title: "Play Next"
                                    imageSource: "asset:///images/ic_play_next.png"
                                    onTriggered: {
                                        ptrack.ListItem.view.queueAdd(ptrack.ListItem.indexPath, true);
                                    }
                                }
                                ActionItem {
                                    title: "Add to Queue"
                                    imageSource: "asset:///images/ic_add_queue.png"
                                    onTriggered: {
                                        ptrack.ListItem.view.queueAdd(ptrack.ListItem.indexPath, false);
                                    }
                                }
                                ActionItem {
                                    title: "Start Radio"
                                    imageSource: "asset:///images/ic_radio.png"
                                    onTriggered: {
                                        ptrack.ListItem.view.radio(ptrack.ListItem.indexPath);
                                    }
                                }
                            }
                        ]
                    }
                }
            ]

            onTriggered: {
                if (indexPath[0] > 0) {
                    page.play(indexPath[0] - 1);
                }
            }
        }

        MiniPlayer {
            navPane: page.navPane
        }
    }

    attachedObjects: SystemToast {
        id: notice
        autoUpdateEnabled: true
    }
}
