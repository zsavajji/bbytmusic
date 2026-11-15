import bb.cascades 1.4

// The Active Frame, the app's card while it is minimised: the album art fills it, and over its
// foot a band of translucent black holds the title, the artist and how far the song is (the
// user, 2026-11-15: "the image as background in cover, with the text overlaid onto it, with a
// black opacity layer"). It takes no touch. One layout for every card: the Q10's is 310x211 px,
// the all-touch phones' 334x396 (Z10) and 319x437 (Z30). The three lines are the guidelines'
// sizes for a card: 35 px, 30 px and 24 px. The art is the file Now Playing uses (544 px), so it
// costs no second fetch.
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
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        background: Color.Black
        layout: DockLayout {
        }

        ImageView {
            imageSource: player.loaded && cover.artFile != "" ? cover.artFile : "asset:///images/art_none_large.png"
            scalingMethod: ScalingMethod.AspectFill
            loadEffect: ImageViewLoadEffect.None
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
        }
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Bottom
            background: Color.create(0.0, 0.0, 0.0, 0.6)
            leftPadding: 10
            rightPadding: 10
            topPadding: 6
            bottomPadding: 8
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
                textStyle.color: Color.create("#c8c8c8")
                topMargin: 0
                bottomMargin: 0
            }
            Label {
                visible: player.loaded
                text: player.fmt(player.positionMs) + " / " + player.fmt(player.durationMs)
                textStyle.fontSize: FontSize.XXSmall
                textStyle.color: Color.create("#c8c8c8")
                topMargin: 0
            }
        }
    }
}
