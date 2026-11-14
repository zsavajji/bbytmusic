import bb.cascades 1.4

// The queue as the engine has it: cover, title, artist and album, the playing row in red. A tap
// jumps. Edit Order puts the list in the system's rearrange mode (drag a row by its grip);
// Shuffle and the Sort actions reorder it in the engine; a long press offers Play Next and Remove.
Page {
    id: page
    property variant navPane
    property bool pushed: false     // pushed from Now Playing: no strip under it
    property variant tracks: []
    property int marked: -1         // the row currently shown as playing
    property string queueId: ""
    property bool editing: false
    property int moveFrom: -1
    property int moveTo: -1

    function esc(s) {
        return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    function art(url) {
        var f = url ? thumbs.file(url, 120) : "";
        return f != "" ? f : "asset:///images/art_none.png";
    }

    function entry(t, current) {
        var sub = t.artist ? t.artist : "";
        if (t.album) {
            sub = sub != "" ? sub + " • " + t.album : t.album;
        }
        return {
            "id": t.id,
            "current": current,
            "title": current ? "<html><span style='color:#ff4e45'>" + esc(t.title) + "</span></html>" : t.title,
            "subtitle": sub,
            "status": current ? "Playing" : (t.duration_s > 0 ? player.fmt(t.duration_s * 1000) : ""),
            "thumbUrl": t.thumb ? t.thumb : "",
            "image": art(t.thumb)
        };
    }

    function rebuild(items) {
        var rows = [];
        marked = player.index;
        for (var i = 0; i < items.length; i ++) {
            rows.push(entry(items[i], i == marked));
        }
        queueModel.clear();
        if (rows.length > 0) {
            queueModel.append(rows);
        }
        tracks = items;
        if (marked > 0) {
            list.scrollToItem([ marked ], ScrollAnimation.None);
        }
    }

    // The engine's queue. A full rebuild puts the list back at the playing row, so the small
    // changes are applied in place: the same order (our own move, come back), a tail appended
    // (the radio), one row gone (Remove).
    function fill(result) {
        var items = result.items ? result.items : [];
        var old = tracks;
        var n = old.length;
        var same = 0;
        while (same < n && same < items.length && old[same].id == items[same].id) {
            same ++;
        }
        if (n > 0 && same == n && items.length >= n) {
            var rows = [];
            for (var i = n; i < items.length; i ++) {
                rows.push(entry(items[i], false));
            }
            if (rows.length > 0) {
                queueModel.append(rows);
            }
            tracks = items;
            mark();
            return;
        }
        if (n > 1 && items.length == n - 1) {
            var rest = true;
            for (var k = same; k < items.length; k ++) {
                if (old[k + 1].id != items[k].id) {
                    rest = false;
                    break;
                }
            }
            if (rest) {
                queueModel.removeAt(same);
                tracks = items;
                marked = -1;
                for (var m = 0; m < queueModel.size(); m ++) {
                    if (queueModel.value(m).current) {
                        marked = m;
                    }
                }
                mark();
                return;
            }
        }
        rebuild(items);
    }

    function refresh() {
        queueId = engine.request("queue", {});
    }

    // The current track moved without the queue changing: re-mark two rows, keep the scroll.
    function mark() {
        if (marked == player.index || tracks.length != queueModel.size()) {
            return;
        }
        var old = marked;
        marked = player.index;
        if (old >= 0 && old < tracks.length) {
            queueModel.replace(old, entry(tracks[old], false));
        }
        if (marked >= 0 && marked < tracks.length) {
            queueModel.replace(marked, entry(tracks[marked], true));
        }
    }

    // A row dragged from one place to another: the same move in our copy of the queue, so that
    // the engine's answer finds nothing to rebuild.
    function moved(from, to) {
        var copy = [];
        for (var i = 0; i < tracks.length; i ++) {
            copy.push(tracks[i]);
        }
        var t = copy.splice(from, 1)[0];
        copy.splice(to, 0, t);
        tracks = copy;
        for (var m = 0; m < queueModel.size(); m ++) {
            if (queueModel.value(m).current) {
                marked = m;
            }
        }
    }

    function setEditing(on) {
        editing = on;
        list.rearrangeHandler.setActive(on);
    }

    function sort(by) {
        engine.request("queue_sort", {
            "by": by
        });
    }

    function onEvent(name, fields) {
        if (name == "queue") {
            page.refresh();
        } else if (name == "queue_snapshot") {
            page.fill(fields);
        }
    }

    function onReply(id, ok, result, code, message) {
        if (ok && id != "" && id == page.queueId) {
            page.queueId = "";
            page.fill(result);
        }
    }

    // ponytail: every row is looked at for each thumbnail that arrives; an index by url if a
    // queue of hundreds ever stutters.
    function onThumb(url, size, file) {
        if (size != 120) {
            return;
        }
        for (var i = 0; i < queueModel.size(); i ++) {
            var row = queueModel.value(i);
            if (row.thumbUrl == url && row.image != file) {
                row.image = file;
                queueModel.replace(i, row);
            }
        }
    }

    // Called by main.qml when the page has been popped.
    function cleanup() {
        try {
            engine.event.disconnect(page.onEvent);
            engine.reply.disconnect(page.onReply);
            thumbs.ready.disconnect(page.onThumb);
            player.changed.disconnect(page.mark);
        } catch (e) {
        }
    }

    onCreationCompleted: {
        engine.event.connect(page.onEvent);
        engine.reply.connect(page.onReply);
        thumbs.ready.connect(page.onThumb);
        player.changed.connect(page.mark);
        if (engine.connected) {
            page.refresh();
        }
    }

    // Letter shortcuts are safe here: no text field on this page.
    actions: [
        ActionItem {
            title: page.editing ? "Done" : "Edit Order"
            imageSource: "asset:///images/ic_reorder.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            shortcuts: Shortcut {
                key: "E"
            }
            onTriggered: {
                page.setEditing(! page.editing);
            }
        },
        ActionItem {
            title: "Shuffle"
            imageSource: "asset:///images/ic_shuffle.png"
            ActionBar.placement: ActionBarPlacement.Signature
            shortcuts: Shortcut {
                key: "S"
            }
            onTriggered: {
                engine.request("queue_shuffle", {});
            }
        },
        ActionItem {
            title: "Sort by Title"
            imageSource: "asset:///images/ic_sort.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                page.sort("title");
            }
        },
        ActionItem {
            title: "Sort by Artist"
            imageSource: "asset:///images/ic_sort.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                page.sort("artist");
            }
        },
        ActionItem {
            title: "Sort by Album"
            imageSource: "asset:///images/ic_sort.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                page.sort("album");
            }
        },
        ActionItem {
            title: "Sort by Length"
            imageSource: "asset:///images/ic_sort.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                page.sort("duration");
            }
        },
        ActionItem {
            title: player.playing ? "Pause" : "Play"
            imageSource: player.playing ? "asset:///images/ic_pause.png" : "asset:///images/ic_play.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            shortcuts: Shortcut {
                key: "Space"
            }
            onTriggered: {
                engine.request("toggle", {});
            }
        },
        ActionItem {
            title: "Next"
            imageSource: "asset:///images/ic_next.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            shortcuts: Shortcut {
                key: "N"
            }
            onTriggered: {
                engine.request("next", {});
            }
        },
        ActionItem {
            title: "Previous"
            imageSource: "asset:///images/ic_previous.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            shortcuts: Shortcut {
                key: "P"
            }
            onTriggered: {
                engine.request("prev", {});
            }
        },
        // Everything but the track that is playing.
        DeleteActionItem {
            title: "Clear"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                engine.request("queue_clear", {});
            }
        }
    ]

    Container {
        layout: StackLayout {
        }

        ConnectionBar {
        }
        Header {
            title: page.editing ? "Drag a row to move it" : "Queue"
            subtitle: page.tracks.length == 1 ? "1 track" : page.tracks.length + " tracks"
        }
        Label {
            text: "The queue is empty. Play something from Search."
            multiline: true
            visible: page.tracks.length == 0
            horizontalAlignment: HorizontalAlignment.Center
        }
        ListView {
            id: list
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            dataModel: ArrayDataModel {
                id: queueModel
            }

            // The system's rearrange mode: the move is made in the model as the finger goes, and
            // told to the engine once, when the row is dropped.
            rearrangeHandler {
                onMoveStarted: {
                    page.moveFrom = event.startIndexPath[0];
                    page.moveTo = page.moveFrom;
                }
                onMoveUpdated: {
                    queueModel.move(event.fromIndexPath[0], event.toIndexPath[0]);
                    page.moveTo = event.toIndexPath[0];
                }
                onMoveEnded: {
                    if (page.moveFrom >= 0 && page.moveTo != page.moveFrom) {
                        page.moved(page.moveFrom, page.moveTo);
                        engine.request("queue_move", {
                            "from": page.moveFrom,
                            "to": page.moveTo
                        });
                    }
                    page.moveFrom = -1;
                }
                onMoveAborted: {
                    // Where the row was left is where it stays: tell the engine the same.
                    if (page.moveFrom >= 0 && page.moveTo != page.moveFrom) {
                        page.moved(page.moveFrom, page.moveTo);
                        engine.request("queue_move", {
                            "from": page.moveFrom,
                            "to": page.moveTo
                        });
                    }
                    page.moveFrom = -1;
                }
            }

            // Called by the rows' context actions (list items cannot see this page's ids).
            function remove(index) {
                engine.request("queue_remove", {
                    "index": index
                });
            }
            // Right after the current track; "to" is the index the row ends up at.
            function playNext(index) {
                var cur = player.index;
                if (index == cur) {
                    return;
                }
                engine.request("queue_move", {
                    "from": index,
                    "to": index > cur ? cur + 1 : cur
                });
            }

            listItemComponents: ListItemComponent {
                StandardListItem {
                    id: row
                    title: ListItemData.title
                    description: ListItemData.subtitle
                    status: ListItemData.status
                    imageSource: ListItemData.image
                    textFormat: TextFormat.Auto
                    contextActions: [
                        ActionSet {
                            title: ListItemData.subtitle
                            ActionItem {
                                title: "Play Next"
                                imageSource: "asset:///images/ic_play_next.png"
                                onTriggered: {
                                    row.ListItem.view.playNext(row.ListItem.indexPath[0]);
                                }
                            }
                            DeleteActionItem {
                                title: "Remove"
                                onTriggered: {
                                    row.ListItem.view.remove(row.ListItem.indexPath[0]);
                                }
                            }
                        }
                    ]
                }
            }

            onTriggered: {
                if (! page.editing) {
                    engine.request("jump", {
                        "index": indexPath[0]
                    });
                }
            }
        }

        MiniPlayer {
            navPane: page.navPane
            visible: player.loaded && ! page.pushed
        }
    }
}
