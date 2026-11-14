import bb.cascades 1.2

// The bar at the bottom of a page: title, artist, Play/Pause; a tap on the text opens Now Playing
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
    Button {
        text: player.playing ? "Pause" : "Play"
        preferredWidth: ui.du(18)
        verticalAlignment: VerticalAlignment.Center
        onClicked: {
            engine.request("toggle", {});
        }
    }
    Button {
        text: "Next"
        preferredWidth: ui.du(14)
        verticalAlignment: VerticalAlignment.Center
        onClicked: {
            engine.request("next", {});
        }
    }

    attachedObjects: ComponentDefinition {
        id: nowPlaying
        source: "NowPlayingPage.qml"
    }
}
