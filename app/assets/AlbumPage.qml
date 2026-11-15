import bb.cascades 1.4
import bb.system 1.2

// An album's page (or a playlist's: the engine's `browse` answers both alike; or a podcast show's,
// whose rows are episodes). The cover with the title, artist, year and length scrolls away with
// the list; under it the tracks. A tap plays the album from that track; Play (the Signature
// action) from the first; Shuffle in a random order. On a show a tap plays that episode alone.
Page {
    id: page
    property variant navPane
    property string browseId: ""
    property string browseReq: ""
    property bool busy: false
    property bool failed: false
    property bool isAlbum: true
    property bool isShow: false
    property string showName: ""
    property variant info            // the page's artist_id and playlist_id
    property variant tracks: []      // its items
    property string albumArtist: ""
    property string moreReq: ""
    property int pages: 0

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

    // The rows of `items`, the first of which is track number `first` + 1.
    function trackRows(items, first) {
        var rows = [];
        for (var i = 0; i < items.length; i ++) {
            var t = items[i];
            var sub;
            if (isShow) {
                sub = t.subtitle ? t.subtitle : "";     // YouTube's "148K views \u2022 Apr 16"
            } else if (isAlbum) {
                // The album's artist is in the header: a row names its own only when it differs.
                sub = t.artist && t.artist != albumArtist ? t.artist : "";
                if (t.plays) {
                    sub = sub != "" ? sub + " \u2022 " + t.plays : t.plays;
                }
            } else {
                sub = t.artist ? t.artist : "";
                if (t.album) {
                    sub = sub != "" ? sub + " \u2022 " + t.album : t.album;
                }
            }
            rows.push({
                "rowType": isAlbum ? "track" : (isShow ? "etrack" : "ptrack"),
                "title": isAlbum ? (first + i + 1) + ".  " + t.title : t.title,
                "subtitle": sub,
                "durationText": t.duration_s > 0 ? player.fmt(t.duration_s * 1000) : "",
                "thumbUrl": ! isAlbum && t.thumb ? t.thumb : "",
                "image": isAlbum ? "" : art(t.thumb, 120, "asset:///images/art_none.png")
            });
        }
        return rows;
    }

    // A long playlist comes a page at a time: the next one is asked for as soon as one is in,
    // so that Play has the whole list. ponytail: at most 20 pages (about 2000 tracks).
    function more(continuation) {
        if (continuation && pages < 20) {
            pages ++;
            moreReq = engine.request("browse_more", {
                "continuation": continuation
            });
        }
    }

    function fill(result) {
        // A local copy: reading the `tracks` property converts the whole list each time.
        var items = result.items ? result.items : [];
        isAlbum = result.kind == "album";
        isShow = result.kind == "show";
        showName = result.title ? result.title : "";
        albumArtist = result.artist ? result.artist : "";
        tracks = items;
        // The header's facts only: the tracks are in `tracks`.
        info = {
            "artist_id": result.artist_id ? result.artist_id : "",
            "playlist_id": result.playlist_id ? result.playlist_id : ""
        };
        var rows = [ {
                "rowType": "head",
                "title": result.title,
                "artist": albumArtist,
                "subtitle": result.subtitle ? result.subtitle : "",
                "second": result.second_subtitle ? result.second_subtitle : "",
                "thumbUrl": result.thumb ? result.thumb : "",
                "image": art(result.thumb, 226, "asset:///images/art_none_large.png")
            } ];
        rows = rows.concat(trackRows(items, 0));
        trackModel.clear();
        trackModel.append(rows);
        pages = 0;
        more(result.continuation);
    }

    function fillMore(result) {
        var items = result.items ? result.items : [];
        if (items.length == 0) {
            return;
        }
        if (isShow) {
            // The next episodes name no show (the engine named the first page's): it is this page's.
            for (var k = 0; k < items.length; k ++) {
                items[k].artist = showName;
                items[k].album_id = browseId;
            }
        }
        var all = tracks;
        trackModel.append(trackRows(items, all.length));
        tracks = all.concat(items);
        more(result.continuation);
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

    // A show's episode on its own: no list to run on into, no radio.
    function playOne(index) {
        engine.request("play", {
            "items": [ tracks[index] ],
            "index": 0,
            "radio": false
        });
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

    function viewArtist() {
        if (! info || ! info.artist_id) {
            notice.body = "This artist has no page.";
            notice.show();
            return;
        }
        var p = shelfPage.createObject();
        p.navPane = navPane;
        p.browseId = info.artist_id;
        navPane.push(p);
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
                page.fillMore(result);
            }
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
        ActionItem {
            title: "View Artist"
            imageSource: "asset:///images/ic_artist.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                page.viewArtist();
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
        // While the page loads: the spinner in the middle of what the list will fill (the
        // user, 2026-11-14: it sat at the top).
        Container {
            visible: page.busy
            horizontalAlignment: HorizontalAlignment.Fill
            layout: DockLayout {
            }
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            ActivityIndicator {
                running: page.busy
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(12)
                preferredHeight: ui.du(12)
            }
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
                    type: "etrack"
                    StandardListItem {
                        id: etrack
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
                                        etrack.ListItem.view.queueAdd(etrack.ListItem.indexPath, true);
                                    }
                                }
                                ActionItem {
                                    title: "Add to Queue"
                                    imageSource: "asset:///images/ic_add_queue.png"
                                    onTriggered: {
                                        etrack.ListItem.view.queueAdd(etrack.ListItem.indexPath, false);
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
                    if (page.isShow) {
                        page.playOne(indexPath[0] - 1);
                    } else {
                        page.play(indexPath[0] - 1);
                    }
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
            id: shelfPage
            source: "ShelfPage.qml"
        }
    ]
}
