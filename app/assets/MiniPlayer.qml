import bb.cascades 1.4

// The strip at the bottom of a page: whether it plays, the title on one line, and a line that
// shows how far the track is. A tap opens Now Playing on navPane, where every control is (the
// user, 2026-11-14: "reduced to a strip with just the title to reserve more space"). Hidden when
// nothing is loaded.
Container {
    id: mini
    property variant navPane
    property int stripWidth: 720
    visible: player.loaded
    horizontalAlignment: HorizontalAlignment.Fill
    preferredHeight: ui.du(7.2)     // a fifth taller than the first strip (the user, 2026-11-14)
    background: Color.create("#262626")
    layout: DockLayout {
    }

    function open() {
        if (navPane) {
            var p = nowPlaying.createObject();
            p.navPane = navPane;
            navPane.push(p);
        }
    }

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Center
        leftPadding: ui.du(1)
        rightPadding: ui.du(2)
        layout: StackLayout {
            orientation: LayoutOrientation.LeftToRight
        }
        // Not a button: it says what the player is doing.
        ImageView {
            imageSource: player.playing ? "asset:///images/ic_play.png" : "asset:///images/ic_pause.png"
            preferredWidth: ui.du(4)
            preferredHeight: ui.du(4)
            verticalAlignment: VerticalAlignment.Center
            loadEffect: ImageViewLoadEffect.None
        }
        Label {
            text: player.title
            verticalAlignment: VerticalAlignment.Center
            textStyle.base: SystemDefaults.TextStyles.SmallText
            textStyle.color: Color.White
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
        }
    }
    // How far the track is: YouTube's red line along the top edge.
    Container {
        horizontalAlignment: HorizontalAlignment.Left
        verticalAlignment: VerticalAlignment.Top
        preferredHeight: ui.px(4)
        implicitLayoutAnimationsEnabled: false      // it changes four times a second
        preferredWidth: player.durationMs > 0 ? mini.stripWidth * player.positionMs / player.durationMs : 0
        background: Color.create("#ff0000")
    }

    gestureHandlers: TapHandler {
        onTapped: {
            mini.open();
        }
    }

    attachedObjects: [
        ComponentDefinition {
            id: nowPlaying
            source: "NowPlayingPage.qml"
        },
        LayoutUpdateHandler {
            onLayoutFrameChanged: {
                mini.stripWidth = layoutFrame.width;
            }
        }
    ]
}
