import bb.cascades 1.4
import "queue.js" as Q

// The queue as the engine has it: cover, title, artist and album, the playing row in red. A tap
// jumps. Edit Order puts the list in the system's rearrange mode (drag a row by its grip);
// Shuffle and the Sort actions reorder it in the engine; a long press offers Play Next and Remove.
Page {
    id: page
    property variant navPane
    property bool pushed: false     // opened from Now Playing, not the Queue tab's own page
    property variant tracks: []
    property int marked: -1         // the row currently shown as playing
    property string queueId: ""
    property bool editing: false
    property int moveFrom: -1
    property int moveTo: -1
    property bool stale: false      // the engine's queue changed during a drag

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
        var time = t.duration_s > 0 ? player.fmt(t.duration_s * 1000) : "";
        // In rearrange mode each row's grip lies over its right end, where the time is: the time
        // then leads the line under the title (the user, 2026-11-14).
        var title = t.title;
        if (editing) {
            if (time != "") {
                sub = sub != "" ? time + " \u2022 " + sub : time;
            }
            // Both lines stop short of the grip (it lies over the row's right end from about
            // x 658 on the Q10; the text starts at 98: 26 and 34 units, see queue.js).
            title = Q.fit(title, 26);
            sub = Q.fit(sub, 34);
        }
        return {
            "id": t.id,
            "current": current,
            "title": current ? "<html><span style='color:#ff4e45'>" + esc(title) + "</span></html>" : title,
            "subtitle": sub,
            //: shown on the row of the song that is playing now
            "status": editing ? "" : (current ? qsTr("Playing") : time),
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

    // The row the model shows as playing, or -1.
    function currentRow() {
        for (var m = 0; m < queueModel.size(); m ++) {
            if (queueModel.value(m).current) {
                return m;
            }
        }
        return -1;
    }

    // The engine's queue. A full rebuild puts the list back at the playing row, so the small
    // changes are applied in place (queue.js: diff).
    function fill(result) {
        if (moveFrom >= 0) {
            stale = true;       // not under the finger: dropped() asks again
            return;
        }
        var items = result.items ? result.items : [];
        var change = Q.diff(tracks, items);
        if (change.op == "rebuild") {
            rebuild(items);
            return;
        }
        if (change.op == "append") {
            var rows = [];
            for (var i = change.from; i < items.length; i ++) {
                rows.push(entry(items[i], false));
            }
            queueModel.append(rows);
        } else if (change.op == "remove") {
            queueModel.removeAt(change.at);
            marked = currentRow();
        }
        tracks = items;
        mark();
    }

    function refresh() {
        queueId = engine.request("queue", {});
    }

    // The current track moved without the queue changing: re-mark two rows, keep the scroll.
    // Not while a row is being dragged: the model is then ahead of `tracks`, and the drop
    // calls here again.
    function mark() {
        if (moveFrom >= 0) {
            return;
        }
        var items = tracks;
        if (marked == player.index || items.length != queueModel.size()) {
            return;
        }
        var old = marked;
        marked = player.index;
        if (old >= 0 && old < items.length) {
            queueModel.replace(old, entry(items[old], false));
        }
        if (marked >= 0 && marked < items.length) {
            queueModel.replace(marked, entry(items[marked], true));
        }
    }

    // The row was let go (or the drag was cut short: where it was left is where it stays): the
    // same move in our copy and in the engine, then the playing row, which may have changed.
    function dropped() {
        var from = moveFrom;
        moveFrom = -1;
        if (from >= 0 && moveTo != from) {
            moved(from, moveTo);
            engine.request("queue_move", {
                "from": from,
                "to": moveTo
            });
        } else {
            mark();     // after a move the engine's queue event does it, with the new index
        }
        if (stale) {
            stale = false;
            refresh();
        }
    }

    // A row dragged from one place to another: the same move in our copy of the queue, so that
    // the engine's answer finds nothing to rebuild.
    function moved(from, to) {
        tracks = Q.moved(tracks, from, to);
        marked = currentRow();
    }

    function setEditing(on) {
        if (! on && moveFrom >= 0) {
            dropped();      // Done under a drag: the row stays where it is
        }
        editing = on;
        list.rearrangeHandler.setActive(on);
    }

    // Every row again, in place: what a row shows depends on the mode.
    onEditingChanged: {
        var items = tracks;
        if (items.length != queueModel.size()) {
            return;
        }
        for (var i = 0; i < items.length; i ++) {
            queueModel.replace(i, entry(items[i], i == marked));
        }
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
    // One try each: a disconnect that throws must not leave the others connected.
    function cleanup() {
        try {
            engine.event.disconnect(page.onEvent);
        } catch (e0) {
        }
        try {
            engine.reply.disconnect(page.onReply);
        } catch (e1) {
        }
        try {
            thumbs.ready.disconnect(page.onThumb);
        } catch (e2) {
        }
        try {
            player.changed.disconnect(page.mark);
        } catch (e3) {
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
            //: the button that switches the queue to a mode where rows can be dragged (Edit Order), and back (Done)
            title: page.editing ? qsTr("Done") : qsTr("Edit Order")
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
            title: player.shuffle ? qsTr("Shuffle: On") : qsTr("Shuffle: Off")
            imageSource: "asset:///images/ic_shuffle.png"
            // An ordinary action, not the Signature one (the user, 2026-11-14): on the bar when
            // the page is pushed from Now Playing, in the action menu on the Queue tab, whose
            // bar holds the tabs.
            ActionBar.placement: ActionBarPlacement.OnBar
            shortcuts: Shortcut {
                key: "S"
            }
            onTriggered: {
                engine.request("shuffle", {
                    "on": ! player.shuffle
                });
            }
        },
        ActionItem {
            title: qsTr("Sort by Title")
            imageSource: "asset:///images/ic_sort.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                page.sort("title");
            }
        },
        ActionItem {
            title: qsTr("Sort by Artist")
            imageSource: "asset:///images/ic_sort.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                page.sort("artist");
            }
        },
        ActionItem {
            title: qsTr("Sort by Album")
            imageSource: "asset:///images/ic_sort.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                page.sort("album");
            }
        },
        ActionItem {
            title: qsTr("Sort by Length")
            imageSource: "asset:///images/ic_sort.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                page.sort("duration");
            }
        },
        ActionItem {
            //: verbs, the button that pauses or starts playback
            title: player.playing ? qsTr("Pause") : qsTr("Play")
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
            title: qsTr("Next")
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
            title: qsTr("Previous")
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
            //: verb, empties the queue
            title: qsTr("Clear")
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
            title: page.editing ? qsTr("Drag a row to move it") : qsTr("Queue")
            subtitle: qsTr("%n track(s)", "", page.tracks.length)
        }
        Label {
            text: qsTr("The queue is empty. Play something from Search.")
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
                    page.dropped();
                }
                onMoveAborted: {
                    page.dropped();
                }
                // Should the system leave rearrange mode by itself, the action says so.
                onActiveChanged: {
                    if (! active && page.moveFrom >= 0) {
                        page.dropped();     // the mode went away under a drag
                    }
                    page.editing = active;  // after the drop: the rows are redrawn from `tracks`
                }
            }

            // Called by the rows' context actions (list items cannot see this page's ids).
            function remove(index) {
                engine.request("queue_remove", {
                    "index": index
                });
            }
            function favorite(index) {
                engine.request("favorite", {
                    "item": page.tracks[index],
                    "on": true
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
                    "to": Q.nextSlot(index, cur)
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
                                //: puts the song right after the one playing
                                title: qsTr("Play Next")
                                imageSource: "asset:///images/ic_play_next.png"
                                onTriggered: {
                                    row.ListItem.view.playNext(row.ListItem.indexPath[0]);
                                }
                            }
                            ActionItem {
                                title: qsTr("Add to Favorites")
                                imageSource: "asset:///images/ic_favorite.png"
                                onTriggered: {
                                    row.ListItem.view.favorite(row.ListItem.indexPath[0]);
                                }
                            }
                            DeleteActionItem {
                                //: removes the song from the queue
                                title: qsTr("Remove")
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
        }
    }
}
