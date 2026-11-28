import bb.cascades 1.4
import "quiet.js" as Quiet

// Docked mode (docs/plan/docked.md): the page for a TV or any external display, which shows what the
// phone shows. The artwork across the whole screen under a dark layer, the title and artist at the
// top with a small Close, the lyrics in the middle when on, and large controls at the bottom (no scrubber: the user, 2026-11-27).
// The button row is 76 du with its margins: the Q10 is 80 du wide (9 px per du).
// It takes the scene as Now Playing does (present() / leave()) and is made by main.qml, which owns
// the definition and opens it by hand (Now Playing's action) or by itself when a display is attached.
Page {
    id: page
    property variant backScene      // what was on screen before: put back when this page leaves
    property variant host           // the root TabbedPane, told when this page is gone (dockClosed)
    property bool leaving: false
    property string artFile: ""
    property int artSize: 720       // the screen's longer side in pixels, capped at 1440
    property bool favorite: false
    property string favId: ""       // the track the flag is for
    property string favReq: ""
    property bool lyricsOn: false
    property string lyricsReq: ""
    property string lyricsFor: ""   // the track the lyrics are for
    // With lyrics up, one colour from the cover fills the page in place of the cover (the user,
    // 2026-11-27: their example, and "background should not be in the lyrics box only").
    property bool showLyrics: page.lyricsOn && (lyricsView.phase == "" || lyricsView.phase == "loading")
    property string tint: ""
    onArtFileChanged: {
        page.tint = page.artFile != "" ? thumbs.tint(page.artFile) : "";
    }

    actionBarVisibility: ChromeVisibility.Hidden

    // The way out, in the drawer from the top (the user, 2026-11-27): no Close on the page.
    Menu.definition: MenuDefinition {
        actions: [
            ActionItem {
                title: qsTr("Exit Docked Mode")
                imageSource: "asset:///images/ic_clear.png"
                onTriggered: {
                    page.leave();
                }
            }
        ]
    }

    // Letter shortcuts are safe here: no text field on this page.
    shortcuts: [
        Shortcut {
            key: "Space"
            onTriggered: {
                engine.request("toggle", {});
            }
        },
        Shortcut {
            key: "N"
            onTriggered: {
                engine.request("next", {});
            }
        },
        Shortcut {
            key: "P"
            onTriggered: {
                engine.request("prev", {});
            }
        },
        Shortcut {
            key: "L"
            onTriggered: {
                page.toggleLyrics();
            }
        }
    ]

    // ponytail: copied from NowPlayingPage; move the request into LyricsView if a third page needs lyrics.
    function askLyrics() {
        var id = player.track.id;
        if (! page.lyricsOn || ! id || id == page.lyricsFor) {
            return;
        }
        page.lyricsFor = id;
        lyricsView.clear("loading");
        page.lyricsReq = engine.request("lyrics", {
            "id": id
        });
        Quiet.add(page.lyricsReq);      // the view says it when there are none: no toast
    }

    function toggleLyrics() {
        page.lyricsOn = ! page.lyricsOn;
        if (page.lyricsOn) {
            page.askLyrics();
            lyricsView.update(player.positionMs);
        }
    }

    function toggleFavorite() {
        if (! player.track.id)
            return;
        if (page.favorite) {
            engine.request("favorite", {
                "id": player.track.id,
                "on": false
            });
        } else {
            engine.request("favorite", {
                "item": player.track,
                "on": true
            });
        }
    }

    // Asks whether the track now playing is a favorite; the id guard makes repeated calls free.
    function askFavorite() {
        var id = player.track.id;
        if (! id || id == page.favId) {
            return;
        }
        page.favId = id;
        page.favorite = false;
        page.favReq = engine.request("favorite", {
            "id": id
        });
    }

    // The position moves every 250 ms: the lit line.
    function onPosition() {
        if (page.lyricsOn) {
            lyricsView.update(player.positionMs);
        }
    }

    // The large picture takes seconds to come (1.0.3.90 showed the empty note meanwhile): Now
    // Playing's 544 px one, already in the cache, stands in until it does.
    function refreshArt() {
        var big = thumbs.file(player.thumb, page.artSize);
        artFile = big != "" ? big : thumbs.file(player.thumb, 544);
    }

    function onThumb(url, size, file) {
        if (url == player.thumb && (size == page.artSize || (size == 544 && page.artFile == ""))) {
            artFile = file;
        }
    }

    function onPlayer() {
        refreshArt();
        askFavorite();
        askLyrics();
    }

    function onReply(id, ok, result, code, message) {
        if (id == "") {
            return;
        }
        if (id == page.lyricsReq) {
            page.lyricsReq = "";
            if (ok && result.id == page.lyricsFor) {
                lyricsView.load(result);
                lyricsView.update(player.positionMs);
            } else if (! ok) {
                lyricsView.clear(code == "not_found" ? "none" : "error");
            }
        } else if (id == page.favReq) {
            page.favReq = "";
            page.favorite = ok && result.on && result.id == page.favId;
        }
    }

    function onEvent(name, fields) {
        if (name == "favorite" && fields.id == page.favId) {
            page.favorite = fields.on;
        } else if (name == "status") {
            page.favId = "";
            page.askFavorite();
            page.lyricsFor = "";    // the engine may have restarted: ask again
            page.askLyrics();
        }
    }

    // Takes the whole screen and keeps it lit: a TV is not touched, and would go dark.
    function present() {
        backScene = Application.scene;
        Application.scene = page;
        Application.mainWindow.screenIdleMode = ScreenIdleMode.KeepAwake;
        fadeIn.play();
    }

    // Never give the scene back from inside a button's handler (it closed the app once, see
    // NowPlayingPage.openBelow): only from the fade's end.
    function leave() {
        if (! leaving) {
            leaving = true;
            fadeOut.play();
        }
    }

    function leaveNow() {
        Application.mainWindow.screenIdleMode = ScreenIdleMode.Normal;
        if (backScene) {
            Application.scene = backScene;
            backScene = undefined;
        }
        if (host) {
            host.dockClosed(page);
        }
        cleanup();
        page.destroy();
    }

    // Called when the page leaves the screen.
    // One try each: a disconnect that throws must not leave the others connected.
    function cleanup() {
        try {
            player.positionChanged.disconnect(page.onPosition);
        } catch (e0) {
        }
        try {
            player.changed.disconnect(page.onPlayer);
        } catch (e1) {
        }
        try {
            thumbs.ready.disconnect(page.onThumb);
        } catch (e2) {
        }
        try {
            engine.reply.disconnect(page.onReply);
        } catch (e3) {
        }
        try {
            engine.event.disconnect(page.onEvent);
        } catch (e4) {
        }
    }

    onCreationCompleted: {
        player.positionChanged.connect(page.onPosition);
        player.changed.connect(page.onPlayer);
        thumbs.ready.connect(page.onThumb);
        engine.reply.connect(page.onReply);
        engine.event.connect(page.onEvent);
        page.onPlayer();
    }

    Container {
        id: rootBox
        layout: DockLayout {
        }
        background: Color.Black
        animations: [
            FadeTransition {
                id: fadeIn
                fromOpacity: 0
                toOpacity: 1
                duration: 300
            },
            FadeTransition {
                id: fadeOut
                toOpacity: 0
                duration: 220
                onEnded: {
                    page.leaveNow();
                }
            }
        ]
        attachedObjects: [
            // The artwork is asked at the screen's own size, whichever phone or display it is.
            LayoutUpdateHandler {
                onLayoutFrameChanged: {
                    var side = Math.round(Math.min(1440, Math.max(layoutFrame.width, layoutFrame.height)));
                    if (side > 0 && side != page.artSize) {
                        page.artSize = side;
                        page.refreshArt();
                    }
                }
            }
        ]

        ImageView {
            visible: ! page.showLyrics
            imageSource: page.artFile != "" ? page.artFile : "asset:///images/art_none_large.png"
            scalingMethod: ScalingMethod.AspectFill
            loadEffect: ImageViewLoadEffect.None
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
        }
        // Keeps the text readable over any picture; or the cover's colour, under the lyrics.
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            background: page.showLyrics ? Color.create(page.tint != "" ? page.tint : "#3a3a3a") : Color.create("#99000000")
        }
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            layout: StackLayout {
            }

            // Title, artist and album.
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(3)
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                layout: StackLayout {
                    orientation: LayoutOrientation.LeftToRight
                }
                Container {
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }
                    Label {
                        text: player.title
                        multiline: true
                        textStyle.fontSize: FontSize.XXLarge
                        textStyle.fontWeight: FontWeight.Bold
                        textStyle.color: Color.White
                    }
                    Label {
                        text: player.state == "loading" ? qsTr("Loading...") : player.artist
                        textStyle.fontSize: FontSize.Large
                        textStyle.color: Color.create("#c8c8c8")
                    }
                    Label {
                        text: player.album
                        textStyle.fontSize: FontSize.Medium
                        textStyle.color: Color.create("#969696")
                    }
                }
                // Lyrics and Favorite up here, so the three below can be large (the user,
                // 2026-11-27: "make the buttons bigger"): the row has 80 du on the Q10 and the Passport.
                // One above the other ("in a column not in a row").
                Container {
                    leftPadding: ui.du(1.5)
                    ImageButton {
                        defaultImageSource: "asset:///images/ic_lyrics_big.png"
                        accessibility.name: page.lyricsOn ? qsTr("Hide Lyrics") : qsTr("Show Lyrics")
                        opacity: page.lyricsOn ? 1.0 : 0.4
                        preferredWidth: ui.du(10)
                        preferredHeight: ui.du(10)
                        onClicked: {
                            page.toggleLyrics();
                        }
                    }
                    ImageButton {
                        defaultImageSource: "asset:///images/ic_favorite_big.png"
                        accessibility.name: page.favorite ? qsTr("Remove from Favorites") : qsTr("Add to Favorites")
                        opacity: page.favorite ? 1.0 : 0.4
                        preferredWidth: ui.du(10)
                        preferredHeight: ui.du(10)
                        onClicked: {
                            page.toggleFavorite();
                        }
                    }
                }
            }

            // What is left: the artwork shows through, or the lyrics are over it, a little apart
            // from the artist and the time (the user, 2026-11-27).
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                topPadding: ui.du(3)
                bottomPadding: ui.du(3)
                layout: DockLayout {
                }
                layoutProperties: StackLayoutProperties {
                    spaceQuota: 1
                }
                LyricsView {
                    id: lyricsView
                    visible: page.showLyrics
                    bottomSpace: 0      // the lyrics have an area of their own here
                    big: true           // read from across a room (the user, 2026-11-27: "bigger")
                    background: Color.Transparent       // the page's colour is the backdrop
                    onSeekTo: {
                        engine.request("seek", {
                            "ms": ms
                        });
                    }
                }
            }

            // The time and the large buttons.
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                leftPadding: ui.du(3)
                rightPadding: ui.du(3)
                bottomPadding: ui.du(3)
                Label {
                    text: player.fmt(player.positionMs) + (player.durationMs > 0 ? " / " + player.fmt(player.durationMs) : "")
                    horizontalAlignment: HorizontalAlignment.Center
                    textStyle.fontSize: FontSize.XLarge     // "bigger" (the user, 2026-11-27)
                    textStyle.color: Color.create("#c8c8c8")
                }
                Container {
                    horizontalAlignment: HorizontalAlignment.Center
                    topPadding: ui.du(1)
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    ImageButton {
                        defaultImageSource: "asset:///images/ic_previous_big.png"
                        accessibility.name: qsTr("Previous")
                        preferredWidth: ui.du(20)
                        preferredHeight: ui.du(20)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(1)
                        rightMargin: ui.du(1)
                        onClicked: {
                            engine.request("prev", {});
                        }
                    }
                    // While a track loads, Play/Pause fades into a spinner in its place (the user,
                    // 2026-11-27); the spinner in the middle of the page went.
                    Container {
                        layout: DockLayout {
                        }
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(1)
                        rightMargin: ui.du(1)
                        ImageButton {
                            defaultImageSource: player.playing ? "asset:///images/ic_pause_big.png" : "asset:///images/ic_play_big.png"
                            accessibility.name: player.playing ? qsTr("Pause") : qsTr("Play")
                            preferredWidth: ui.du(24)
                            preferredHeight: ui.du(24)
                            opacity: player.state == "loading" ? 0 : 1
                            onClicked: {
                                engine.request("toggle", {});
                            }
                        }
                        ActivityIndicator {
                            running: player.state == "loading"
                            opacity: player.state == "loading" ? 1 : 0
                            horizontalAlignment: HorizontalAlignment.Center
                            verticalAlignment: VerticalAlignment.Center
                            preferredWidth: ui.du(16)
                            preferredHeight: ui.du(16)
                        }
                    }
                    ImageButton {
                        defaultImageSource: "asset:///images/ic_next_big.png"
                        accessibility.name: qsTr("Next")
                        preferredWidth: ui.du(20)
                        preferredHeight: ui.du(20)
                        verticalAlignment: VerticalAlignment.Center
                        leftMargin: ui.du(1)
                        rightMargin: ui.du(1)
                        onClicked: {
                            engine.request("next", {});
                        }
                    }
                }
            }
        }
    }
}
