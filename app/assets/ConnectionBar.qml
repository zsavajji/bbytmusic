import bb.cascades 1.4

// Shown at the top of a page while the engine is not connected.
Container {
    visible: !engine.connected
    layout: StackLayout {
        orientation: LayoutOrientation.LeftToRight
    }
    background: Color.create("#5a1a1a")
    leftPadding: ui.du(1)
    rightPadding: ui.du(1)
    topPadding: ui.du(1)
    bottomPadding: ui.du(1)

    Label {
        text: "The engine is not connected."
        verticalAlignment: VerticalAlignment.Center
        layoutProperties: StackLayoutProperties {
            spaceQuota: 1
        }
    }
    Button {
        text: "Retry"
        preferredWidth: ui.du(18)
        onClicked: {
            engine.retry();
        }
    }
}
