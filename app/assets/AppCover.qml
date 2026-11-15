import bb.cascades 1.4

// The Active Frame, the app's card while it is minimised: the album art, the title, the artist,
// and under them how far the song is (the user, 2026-11-15). It takes no touch. The Q10's card is
// 310x211 px and all of it shows; the all-touch phones' are 334x396 (Z10) and 319x437 (Z30)
// (UI Guidelines p.120-124, in the bb10-devices skill). A wide card puts the art at the left and
// the three lines at the right; a tall one the art on top and the lines below. The three lines
// are the guidelines' sizes for a card: 35 px, 30 px and 24 px. The art is the file Now Playing
// uses (544 px), so it costs no second fetch.
SceneCover {
    id: cover
    property string artFile: ""

    function refreshArt() {
        artFile = thumbs.file(player.thumb, 544);
    }

    function onThumb(url, size, file) {
        if (size == 544 && url == player.thumb) {
            artFile = file;
        }
    }

    // The card lives as long as the app: nothing to disconnect.
    onCreationCompleted: {
        player.changed.connect(cover.refreshArt);
        thumbs.ready.connect(cover.onThumb);
        cover.refreshArt();
    }

    content: Container {
        id: frame
        // The card's size in px, the Q10's until the first layout says.
        property int w: 310
        property int h: 211
        property int gap: 8
        property bool tall: h > w
        // A square: about half the width of a wide card (the text needs the rest), about
        // three fifths of the height of a tall one; never past the edges.
        property int art: Math.max(0, Math.min(w - 2 * gap, h - 2 * gap, Math.round(tall ? h * 0.6 : w * 0.46)))
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        background: Color.Black
        leftPadding: gap
        rightPadding: gap
        topPadding: gap
        bottomPadding: gap
        layout: StackLayout {
            orientation: frame.tall ? LayoutOrientation.TopToBottom : LayoutOrientation.LeftToRight
        }

        ImageView {
            imageSource: player.loaded && cover.artFile != "" ? cover.artFile : "asset:///images/art_none_large.png"
            scalingMethod: ScalingMethod.AspectFill
            loadEffect: ImageViewLoadEffect.None
            preferredWidth: frame.art
            preferredHeight: frame.art
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
        }
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Center
            leftPadding: frame.tall ? 0 : frame.gap
            topPadding: frame.tall ? frame.gap : 0
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            Label {
                text: player.loaded ? player.title : "BBMusic"
                textStyle.fontSize: FontSize.Small
                textStyle.color: Color.White
                bottomMargin: 0
            }
            Label {
                visible: player.loaded
                text: player.artist
                textStyle.fontSize: FontSize.XSmall
                textStyle.color: Color.create("#969696")
                topMargin: 0
                bottomMargin: 0
            }
            // Where the card has no room for it, it is cut short rather than dropped.
            Label {
                visible: player.loaded
                text: player.fmt(player.positionMs) + " / " + player.fmt(player.durationMs)
                textStyle.fontSize: FontSize.XXSmall
                textStyle.color: Color.create("#969696")
                topMargin: 0
            }
        }

        attachedObjects: [
            LayoutUpdateHandler {
                onLayoutFrameChanged: {
                    if (layoutFrame.width > 0 && layoutFrame.height > 0) {
                        frame.w = layoutFrame.width;
                        frame.h = layoutFrame.height;
                    }
                }
            }
        ]
    }
}
