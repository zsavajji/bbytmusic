import bb.cascades 1.4
import bbmusic 1.0

// A hint in the look of the BB10 lock screen's yellow tutorial hints, as HyperBerry draws it for a
// first call (its Hint.qml, read off the Q10): a square-cornered box nearly the screen's width,
// #DCD427 at 95 % in a dark frame for a shadow, a white disc with an "i" at its left, dark text.
// The owner places it and calls show(ms); it hides after ms, at a tap, or at hide(). Hidden, it
// takes no room. Sizes are the Q10's px as design units (9 px each), so other screens scale them.
Container {
    id: hint
    property string text
    visible: false
    horizontalAlignment: HorizontalAlignment.Fill
    topPadding: ui.du(1)
    bottomPadding: ui.du(1)
    leftPadding: ui.du(2.7)
    rightPadding: ui.du(2.7)

    function show(ms) {
        hider.stop();
        hider.interval = ms;
        visible = true;
        hider.start();
    }
    function hide() {
        hider.stop();
        visible = false;
    }

    attachedObjects: [
        Timer {
            id: hider
            singleShot: true
            onTimeout: {
                hint.hide();
            }
        }
    ]
    gestureHandlers: TapHandler {
        onTapped: {
            hint.hide();
        }
    }

    // The shadow: a darker frame, a little lower than it is wide.
    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        leftPadding: ui.px(4)
        rightPadding: ui.px(4)
        topPadding: ui.px(2)
        bottomPadding: ui.px(6)
        background: Color.create("#33000000")
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            topPadding: ui.du(2.7)
            bottomPadding: ui.du(2.7)
            leftPadding: ui.du(2.7)
            rightPadding: ui.du(2.7)
            background: Color.create("#f2dcd427")
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            ImageView {
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(5.3)
                preferredHeight: ui.du(5.3)
                minWidth: ui.du(5.3)
                scalingMethod: ScalingMethod.AspectFit
                loadEffect: ImageViewLoadEffect.None
                imageSource: "asset:///images/hint_info.png"
            }
            Label {
                verticalAlignment: VerticalAlignment.Center
                leftMargin: ui.du(2.2)
                layoutProperties: StackLayoutProperties {
                    spaceQuota: 1
                }
                multiline: true
                text: hint.text
                textStyle.color: Color.create("#262626")
            }
        }
    }
}
