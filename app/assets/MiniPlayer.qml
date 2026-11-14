import bb.cascades 1.2

// The bar at the bottom of a page: title, artist, a Play/Pause icon; a tap on the text opens Now Playing
// on navPane. Hidden when nothing is loaded.
Container {
    id: mini
    property variant navPane
    visible: player.loaded
    layout: StackLayout {
        orientation: LayoutOrientation.LeftToRight
    }
    background: Color.create("#282828")
    leftPadding: ui.du(1.5)
    rightPadding: ui.du(1)
    topPadding: ui.du(1)
    bottomPadding: ui.du(1)

    function open() {
        if (navPane) {
            navPane.push(nowPlaying.createObject());
        }
    }

    Container {
        verticalAlignment: VerticalAlignment.Center
        // The text takes all the bar but the icon: it is the way into Now Playing.
        layoutProperties: StackLayoutProperties {
            spaceQuota: 1
        }
        Label {
            text: player.title
            textStyle.base: SystemDefaults.TextStyles.PrimaryText
        }
        Label {
            text: player.artist
            textStyle.base: SystemDefaults.TextStyles.SmallText
        }
        gestureHandlers: TapHandler {
            onTapped: {
                mini.open();
            }
        }
    }
    // One control, an icon: Play or Pause. Everything else is on Now Playing (the user, 0.1.0.6).
    ImageButton {
        defaultImageSource: player.playing ? "asset:///images/ic_pause.png" : "asset:///images/ic_play.png"
        preferredWidth: ui.du(9)
        preferredHeight: ui.du(9)
        verticalAlignment: VerticalAlignment.Center
        onClicked: {
            engine.request("toggle", {});
        }
    }

    attachedObjects: ComponentDefinition {
        id: nowPlaying
        source: "NowPlayingPage.qml"
    }
}
