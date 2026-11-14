import bb.cascades 1.2

// Title, artist, album, a scrubber, the times and Previous / Play-Pause / Next.
Page {
    id: page
    // While a finger is on the slider, and a moment after it lets go, the position updates leave it be.
    property bool dragging: false
    property bool setting: false
    property bool moved: false
    property double holdUntil: 0

    function sync() {
        if (dragging || new Date().getTime() < holdUntil) {
            return;
        }
        setting = true;
        slider.value = Math.min(player.positionMs, slider.toValue);
        setting = false;
    }

    actions: [
        // Letter shortcuts are safe here: no text field on this page.
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
        leftPadding: ui.du(2)
        rightPadding: ui.du(2)
        topPadding: ui.du(2)

        Label {
            text: player.title
            multiline: true
            textStyle.base: SystemDefaults.TextStyles.BigText
        }
        Label {
            text: player.artist
            textStyle.base: SystemDefaults.TextStyles.SubtitleText
        }
        Label {
            text: player.album
            visible: player.album != ""
            textStyle.base: SystemDefaults.TextStyles.SmallText
        }
        Label {
            text: player.state == "loading" ? "Loading..." : player.error
            visible: text != ""
            textStyle.base: SystemDefaults.TextStyles.SmallText
        }

        Slider {
            id: slider
            topMargin: ui.du(4)
            fromValue: 0
            toValue: Math.max(player.durationMs, 1)
            // A seek is sent for a finger only. The value also changes when the page sets it and
            // when toValue moves with the track's length: seeking on valueChanged restarted the
            // song each time this page opened (0.1.0.4).
            onTouch: {
                if (event.touchType == TouchType.Down) {
                    page.dragging = true;
                    page.moved = false;
                } else if (event.touchType == TouchType.Up || event.touchType == TouchType.Cancel) {
                    if (page.dragging && page.moved) {
                        page.holdUntil = new Date().getTime() + 800;
                        engine.request("seek", {
                            "ms": Math.round(slider.immediateValue)
                        });
                    }
                    page.dragging = false;
                }
            }
            onImmediateValueChanged: {
                if (page.dragging && !page.setting) {
                    page.moved = true;
                }
            }
        }
        Container {
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            Label {
                text: player.fmt(page.dragging ? slider.immediateValue : player.positionMs)
                textStyle.base: SystemDefaults.TextStyles.SmallText
                layoutProperties: StackLayoutProperties {
                    spaceQuota: 1
                }
            }
            Label {
                text: player.fmt(player.durationMs)
                textStyle.base: SystemDefaults.TextStyles.SmallText
            }
        }

        Container {
            topMargin: ui.du(4)
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            horizontalAlignment: HorizontalAlignment.Center
            Button {
                text: "Previous"
                preferredWidth: ui.du(24)
                onClicked: {
                    engine.request("prev", {});
                }
            }
            Button {
                text: player.playing ? "Pause" : "Play"
                preferredWidth: ui.du(24)
                onClicked: {
                    engine.request("toggle", {});
                }
            }
            Button {
                text: "Next"
                preferredWidth: ui.du(24)
                onClicked: {
                    engine.request("next", {});
                }
            }
        }
    }

    onCreationCompleted: {
        player.positionChanged.connect(page.sync);
        player.changed.connect(page.sync);
        page.sync();
    }
}
