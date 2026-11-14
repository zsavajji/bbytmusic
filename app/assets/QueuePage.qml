import bb.cascades 1.2

// The queue as the engine has it, the current track marked "Playing". Tap jumps; a long press
// offers Play next and Remove.
Page {
    id: page
    property variant navPane
    property variant tracks: []
    property int marked: -1       // the row currently shown as playing
    property string queueId: ""

    function entry(i, current) {
        var t = tracks[i];
        return {
            "title": t.title,
            "subtitle": t.artist ? t.artist : "",
            "status": current ? "Playing" : (t.duration_s > 0 ? player.fmt(t.duration_s * 1000) : "")
        };
    }

    function fill(result) {
        tracks = result.items ? result.items : [];
        var rows = [];
        marked = player.index;
        for (var i = 0; i < tracks.length; i ++) {
            rows.push(entry(i, i == marked));
        }
        queueModel.clear();
        if (rows.length > 0) {
            queueModel.append(rows);
        }
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
            queueModel.replace(old, entry(old, false));
        }
        if (marked >= 0 && marked < tracks.length) {
            queueModel.replace(marked, entry(marked, true));
        }
    }

    onCreationCompleted: {
        engine.event.connect(function (name, fields) {
            if (name == "queue") {
                page.refresh();
            } else if (name == "queue_snapshot") {
                page.fill(fields);
            }
        });
        engine.reply.connect(function (id, ok, result, code, message) {
            if (ok && id != "" && id == page.queueId) {
                page.queueId = "";
                page.fill(result);
            }
        });
        player.changed.connect(page.mark);
        if (engine.connected) {
            page.refresh();
        }
    }

    // Letter shortcuts are safe here: no text field on this page.
    actions: [
        ActionItem {
            title: player.playing ? "Pause" : "Play"
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
            ActionBar.placement: ActionBarPlacement.InOverflow
            shortcuts: Shortcut {
                key: "P"
            }
            onTriggered: {
                engine.request("prev", {});
            }
        }
    ]

    Container {
        layout: StackLayout {
        }

        ConnectionBar {
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

            // Called by the rows' context actions (list items cannot see this page's ids).
            function remove(index) {
                engine.request("queue_remove", {
                    "index": index
                });
            }
            // Right after the current track. (Guess: "to" is the index the row ends up at.)
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
                    contextActions: [
                        ActionSet {
                            title: ListItemData.title
                            ActionItem {
                                title: "Play next"
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
                engine.request("jump", {
                    "index": indexPath[0]
                });
            }
        }

        MiniPlayer {
            navPane: page.navPane
        }
    }
}
