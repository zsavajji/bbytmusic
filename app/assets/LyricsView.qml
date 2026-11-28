import bb.cascades 1.4

// The lyrics of the track now playing, laid over its artwork (NowPlayingPage.qml asks the engine
// and hands the answer to load()). Synced lines: the current one white and larger, the others
// grey, the list following it until a finger scrolls it or the Sync button is turned off; a tap
// on a line asks for a seek. Plain text only scrolls.
Container {
    id: view
    // "" (the lines are there), "loading", "none" or "error"
    property string phase: ""
    property bool synced: false
    property bool autoSync: true
    property int line: -1               // the lit line, -1 before the first
    property variant lines: []          // [{ms, text}] as the engine sent them
    property double downY: 0
    property double bottomSpace: 0     // the scrubber's height: the lines stop above it (the user, 2026-11-27)
    // Docked mode (DockPage.qml): three labels in place of the list, the sung line, the current one
    // and the coming ones, so the current one stays in the middle whatever its height (the user,
    // 2026-11-28: on the Classic the list left it cut).
    property bool big: false
    property bool compact: false        // a 720 px square screen (Q10, Classic): docked mode one size down
    property string pastText: ""
    property string nowText: ""
    property string nextText: ""
    signal seekTo(int ms)

    horizontalAlignment: HorizontalAlignment.Fill
    verticalAlignment: VerticalAlignment.Fill
    background: Color.create("#e0000000")
    layout: DockLayout {
    }

    // Empties the list and says why (a new track is being asked for, or there is nothing).
    function clear(why) {
        model.clear();
        view.lines = [];
        view.line = -1;
        view.synced = false;
        view.autoSync = true;
        view.phase = why;
        showBig();
    }

    function showBig() {
        var all = view.lines;       // a variant: each read of it copies the list
        var i = view.line;
        var next = [];
        for (var j = i + 1; j < all.length && j <= i + 6; ++j) {
            next.push(all[j].text);
        }
        view.pastText = i > 0 ? all[i - 1].text : "";
        view.nowText = i >= 0 && i < all.length ? all[i].text : "";
        view.nextText = next.join("\n");
    }

    // The engine's answer: the lines, then the credit.
    function load(result) {
        // Docked mode: synced lines or nothing (the user, 2026-11-27: "either synchro mode or nothing").
        if (view.big && ! result.synced) {
            clear("none");
            return;
        }
        clear("");
        view.lines = result.lines;
        view.synced = result.synced;
        if (view.big) {
            showBig();
            return;
        }
        var rows = [];
        for (var i = 0; i < result.lines.length; ++i) {
            rows.push({
                "text": result.lines[i].text,
                "kind": result.synced ? "line" : "plain",
                "on": false
            });
        }
        if (result.credit) {
            rows.push({
                "text": result.credit,
                "kind": "credit"
            });
        }
        model.append(rows);
    }

    function light(i, on) {
        model.replace(i, {
            "text": view.lines[i].text,
            "kind": "line",
            "on": on
        });
    }

    function follow() {
        if (view.line >= 0) {
            list.scrollToItem([ Math.max(0, view.line - 2) ], ScrollAnimation.Smooth);
        }
    }

    // Lights the line at `ms` (a little early, so it shows as it is sung): the last one whose time
    // has come, found by halving.
    function update(ms) {
        var all = view.lines;       // a variant: each read of it copies the list
        if (! view.synced || all.length == 0) {
            return;
        }
        var at = ms + 300;
        var lo = -1;
        var hi = all.length - 1;
        while (lo < hi) {
            var mid = Math.ceil((lo + hi) / 2);
            if (all[mid].ms <= at) {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        if (lo == view.line) {
            return;
        }
        var old = view.line;
        view.line = lo;
        if (view.big) {
            showBig();
            return;
        }
        if (old >= 0) {
            light(old, false);
        }
        if (lo >= 0) {
            light(lo, true);
        }
        if (view.autoSync) {
            follow();
        }
    }

    // The Sync button on a row of its own above the lines (the user, 2026-11-27: it covered them).
    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        bottomPadding: view.bottomSpace
        Container {
            visible: view.synced && ! view.big
            horizontalAlignment: HorizontalAlignment.Fill
            topPadding: ui.du(1)
            rightPadding: ui.du(2)
            layout: DockLayout {
            }
            Button {
                text: qsTr("Sync")
                opacity: view.autoSync ? 1.0 : 0.5
                preferredWidth: ui.du(14)
                horizontalAlignment: HorizontalAlignment.Right
                onClicked: {
                    if (view.autoSync) {
                        view.autoSync = false;
                    } else {
                        view.autoSync = true;
                        view.follow();
                    }
                }
            }
        }
        ListView {
            id: list
            visible: ! view.big
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            verticalAlignment: VerticalAlignment.Fill
            dataModel: ArrayDataModel {
                id: model
            }

            function itemType(data, indexPath) {
                return data.kind;
            }

            // A finger that travels turns the following off; a tap does not.
            onTouch: {
                if (event.touchType == TouchType.Down) {
                    view.downY = event.windowY;
                } else if (event.touchType == TouchType.Move || event.touchType == TouchType.Cancel) {
                    if (Math.abs(event.windowY - view.downY) > ui.du(3)) {
                        view.autoSync = false;
                    }
                }
            }

            onTriggered: {
                if (view.synced && indexPath[0] < view.lines.length) {
                    view.autoSync = true;
                    view.seekTo(view.lines[indexPath[0]].ms);
                }
            }

            listItemComponents: [
                ListItemComponent {
                    type: "line"
                    Container {
                        leftPadding: ui.du(3)
                        rightPadding: ui.du(3)
                        topPadding: ui.du(2)
                        bottomPadding: ui.du(2)
                        Label {
                            text: ListItemData.text
                            multiline: true
                            horizontalAlignment: HorizontalAlignment.Fill
                            textStyle.textAlign: TextAlign.Left
                            textStyle.fontSize: ListItemData.on ? FontSize.Large : FontSize.Medium
                            textStyle.fontWeight: FontWeight.Bold
                            textStyle.color: ListItemData.on ? Color.White : Color.create("#969696")
                        }
                    }
                },
                ListItemComponent {
                    type: "plain"
                    Container {
                        leftPadding: ui.du(3)
                        rightPadding: ui.du(3)
                        topPadding: ui.du(2)
                        bottomPadding: ui.du(2)
                        Label {
                            text: ListItemData.text
                            multiline: true
                            horizontalAlignment: HorizontalAlignment.Fill
                            textStyle.textAlign: TextAlign.Left
                            textStyle.fontSize: FontSize.Medium
                            textStyle.fontWeight: FontWeight.Bold
                            textStyle.color: Color.White
                        }
                    }
                },
                ListItemComponent {
                    type: "credit"
                    Container {
                        leftPadding: ui.du(3)
                        rightPadding: ui.du(3)
                        topPadding: ui.du(2)
                        bottomPadding: ui.du(2)
                        Label {
                            text: ListItemData.text
                            multiline: true
                            horizontalAlignment: HorizontalAlignment.Fill
                            textStyle.textAlign: TextAlign.Left
                            textStyle.fontSize: FontSize.XSmall
                            textStyle.color: Color.create("#969696")
                        }
                    }
                }
            ]
        }
    }

    // Docked mode, as the user's example (2026-11-27): one size, lines close together, the sung one
    // faded white, the current one white, the coming ones dark. The equal shares above and below
    // the current line are what centres it. A tap on the sung or the coming lines seeks one line.
    Container {
        visible: view.big && view.synced
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        leftPadding: ui.du(3)
        rightPadding: ui.du(3)
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            layout: DockLayout {
            }
            Label {
                text: view.pastText
                multiline: true
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Bottom
                textStyle.fontSize: view.compact ? FontSize.XLarge : FontSize.XXLarge
                textStyle.fontWeight: FontWeight.Bold
                textStyle.color: Color.White
                opacity: 0.55
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (view.line > 0) {
                                view.seekTo(view.lines[view.line - 1].ms);
                            }
                        }
                    }
                ]
            }
        }
        Label {
            text: view.nowText
            multiline: true
            horizontalAlignment: HorizontalAlignment.Fill
            topMargin: 0
            bottomMargin: 0
            textStyle.fontSize: view.compact ? FontSize.XLarge : FontSize.XXLarge
            textStyle.fontWeight: FontWeight.Bold
            textStyle.color: Color.White
        }
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            layout: DockLayout {
            }
            Label {
                text: view.nextText
                multiline: true
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Top
                textStyle.fontSize: view.compact ? FontSize.XLarge : FontSize.XXLarge
                textStyle.fontWeight: FontWeight.Bold
                textStyle.color: Color.Black
                opacity: 0.7
                gestureHandlers: [
                    TapHandler {
                        onTapped: {
                            if (view.line + 1 < view.lines.length) {
                                view.seekTo(view.lines[view.line + 1].ms);
                            }
                        }
                    }
                ]
            }
        }
    }

    Label {
        text: view.phase == "none" ? qsTr("No lyrics for this song") : qsTr("Lyrics could not be loaded")
        visible: ! view.big && (view.phase == "none" || view.phase == "error")
        multiline: true
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        textStyle.textAlign: TextAlign.Center
        textStyle.fontSize: FontSize.Medium
        textStyle.color: Color.create("#969696")
    }
    ActivityIndicator {
        running: view.phase == "loading"
        visible: view.phase == "loading"
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        preferredWidth: ui.du(12)
        preferredHeight: ui.du(12)
    }
}
