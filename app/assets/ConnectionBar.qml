import bb.cascades 1.4

// Shown at the top of a page once the engine has been gone for 10 s (engine.down), not while it
// is still waking: the pages show their spinner till then.
Container {
    visible: engine.down
    layout: StackLayout {
        orientation: LayoutOrientation.LeftToRight
    }
    background: Color.create("#5a1a1a")
    leftPadding: ui.du(1)
    rightPadding: ui.du(1)
    topPadding: ui.du(1)
    bottomPadding: ui.du(1)

    Label {
        text: qsTr("The engine is not connected.")
        verticalAlignment: VerticalAlignment.Center
        layoutProperties: StackLayoutProperties {
            spaceQuota: 1
        }
    }
    Button {
        text: qsTr("Retry")
        preferredWidth: ui.du(18)
        onClicked: {
            engine.retry();
        }
    }
}
