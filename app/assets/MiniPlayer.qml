import bb.cascades 1.4
import bbmusic 1.0

// The strip at the bottom of a page: the title on one line, the time elapsed and the track's
// length at the far end, over a fill that grows from the left as the track plays. A tap opens Now
// Playing, where every control is (the user, 2026-11-14: "reduced to a strip with just the title to
// reserve more space"): the player takes the screen and its content rises from here
// (NowPlayingPage.present()); what it then opens (an album, the queue) is pushed on navPane.
// Hidden when nothing is loaded.
Container {
    id: mini
    property variant navPane
    property int stripWidth: 720
    property double openedAt: 0
    property variant opening        // the player's page, between the tap and the moment it takes the screen
    visible: player.loaded
    horizontalAlignment: HorizontalAlignment.Fill
    preferredHeight: ui.du(7.2)     // a fifth taller than the first strip (the user, 2026-11-14)
    background: Color.create("#262626")
    layout: DockLayout {
    }

    function open() {
        var now = new Date().getTime();
        if (! navPane || now - openedAt < 800) {     // a double tap opens one player
            return;
        }
        openedAt = now;
        // A child of the scene: a page without a parent would become the Application's own when
        // it takes the screen, and be deleted under its own feet when it gives the screen back.
        var p = nowPlaying.createObject(Application.scene);
        if (p) {
            p.hostPane = navPane;
            // The page under the strip fades out and its bar slides away first, so that neither
            // vanishes or flips at a stroke when the player takes the screen (the user,
            // 2026-11-21); the player puts both back when it leaves.
            var top = navPane.top;
            if (top) {
                p.underPage = top;
                if (top.content) {
                    top.content.opacity = 0;
                }
                top.actionBarVisibility = ChromeVisibility.Hidden;
            }
            opening = p;
            swapTimer.start();
        }
    }

    // How far the track is: the strip fills from the left, a slight red while playing, a lighter
    // grey while paused, so its colour is the state too (the user, 2026-11-15: no icon that reads as
    // a button; "make it fill as the progress goes").
    Container {
        horizontalAlignment: HorizontalAlignment.Left
        verticalAlignment: VerticalAlignment.Fill
        implicitLayoutAnimationsEnabled: false      // it changes four times a second
        preferredWidth: player.durationMs > 0 ? mini.stripWidth * Math.min(1, player.positionMs / player.durationMs) : 0
        background: player.playing ? Color.create("#5c2424") : Color.create("#3a3a3a")
    }

    Container {
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Center
        leftPadding: ui.du(2)
        rightPadding: ui.du(2)
        layout: StackLayout {
            orientation: LayoutOrientation.LeftToRight
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
        // Elapsed and total, at the far end (the user, 2026-11-14). On a page with a Signature
        // action a long title runs behind that button; the user is fine with that.
        Label {
            text: player.fmt(player.positionMs) + " / " + player.fmt(player.durationMs)
            verticalAlignment: VerticalAlignment.Center
            textStyle.base: SystemDefaults.TextStyles.SmallText
            textStyle.color: Color.create("#969696")
        }
        // A tap opens Now Playing, and nothing else says so: this is the hint, not a button (the
        // whole strip takes the tap).
        ImageView {
            imageSource: "asset:///images/ic_expand_up.png"
            filterColor: Color.create("#969696")
            preferredWidth: ui.du(3)
            preferredHeight: ui.du(3)
            minWidth: ui.du(3)
            scalingMethod: ScalingMethod.AspectFit
            loadEffect: ImageViewLoadEffect.None
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(1)
        }
    }
    gestureHandlers: TapHandler {
        onTapped: {
            mini.open();
        }
    }

    attachedObjects: [
        Timer {
            id: swapTimer
            singleShot: true
            interval: 220
            onTimeout: {
                if (mini.opening) {
                    mini.opening.present();
                    mini.opening = undefined;
                }
            }
        },
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
