import bb.cascades 1.4
import bb.system 1.2

// Now Playing, laid out as the stock BB10 player (measured from its screenshots on the Q10):
// title and artist, a row with the album and the Repeat and Shuffle icons, the artwork with the
// scrubber lying over its lower edge, and Previous / Play-Pause / Next on the action bar with
// Play-Pause as the Signature action. What YTMusicWP's player adds is in the action menu: the
// queue, the album, the artist, the track's radio, Share, the equalizer, Properties. (No Stop:
// Pause does that, the user, 2026-11-14.)
Page {
    id: page
    // The page is shown by taking the whole screen (present()), not pushed and not in a sheet: both
    // of those slide in from the right, and the user wanted it to come up from the strip.
    property variant hostPane       // the tab the strip was on: albums, artists and the queue open there
    property variant backScene      // what was on screen before: put back when this page leaves
    property bool leaving: false
    // The bar comes once the content is up: shown from the start, it took the tabs' place at a
    // stroke, before the page had risen (the user, 2026-11-21).
    actionBarVisibility: ChromeVisibility.Hidden
    property string infoReq: ""     // the track's ids are being asked for: it came without them
    property string infoFor: ""     // "album" or "artist": what to open with the answer
    property variant underPage      // the page the strip was on, its content faded out and its bar hidden: put back at the end
    property variant pendingPage    // a page asked for from here: pushed on hostPane once the tabs are back
    property string artFile: ""
    // While a finger is on the slider, and a moment after it lets go, the position updates leave it be.
    property bool dragging: false
    property bool setting: false
    property bool moved: false
    property double holdUntil: 0
    property bool favorite: false
    property string favId: ""       // the track the flag is for
    property string favReq: ""
    property string likeReq: ""
    property string likeSetReq: ""
    property bool liked: false      // on YouTube, for the signed-in account
    property string likeId: ""      // the track the flag is for
    property bool signedIn: false
    property string accountReq: ""

    function sync() {
        if (dragging || new Date().getTime() < holdUntil) {
            return;
        }
        setting = true;
        slider.value = Math.min(player.positionMs, slider.toValue);
        setting = false;
    }

    function refreshArt() {
        artFile = thumbs.file(player.thumb, 544);
    }

    function onThumb(url, size, file) {
        if (size == 544 && url == player.thumb) {
            artFile = file;
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

    // Asks YouTube whether the account likes the track now playing; nothing to ask when signed out.
    function askLike() {
        var id = player.track.id;
        if (! id || ! page.signedIn || id == page.likeId) {
            return;
        }
        page.likeId = id;
        page.liked = false;
        page.likeReq = engine.request("like", {
            "id": id
        });
    }

    function onPlayer() {
        refreshArt();
        sync();
        askFavorite();
        askLike();
    }

    function onReply(id, ok, result, code, message) {
        if (id == "") {
            return;
        }
        if (id == page.infoReq) {
            page.infoReq = "";
            if (ok) {       // a failure is main.qml's to show
                var found = page.infoFor == "album" ? result.album_id : result.artist_id;
                if (found) {
                    if (page.infoFor == "album") {
                        page.openAlbum(found);
                    } else {
                        page.openArtist(found);
                    }
                } else {
                    notice.body = page.infoFor == "album" ? qsTr("This track has no album page.") : qsTr("This artist has no page.");
                    notice.show();
                }
            }
        } else if (id == page.favReq) {
            page.favReq = "";
            page.favorite = ok && result.on && result.id == page.favId;
        } else if (id == page.accountReq) {
            page.accountReq = "";
            page.signedIn = ok && result.state == "in";
            page.askLike();
        } else if (id == page.likeReq) {
            page.likeReq = "";
            if (ok && result.id == page.likeId) {
                page.liked = result.on;
            }
        } else if (id == page.likeSetReq) {
            page.likeSetReq = "";
            if (ok) {
                if (result.id == page.likeId) {
                    page.liked = result.on;
                }
                notice.body = result.on ? qsTr("Liked on YouTube") : qsTr("Like removed on YouTube");
                notice.show();
            }
        }
    }

    function onEvent(name, fields) {
        if (name == "favorite" && fields.id == page.favId) {
            page.favorite = fields.on;
        } else if (name == "account") {
            page.signedIn = fields.state == "in";
            page.likeId = "";
            page.liked = false;
            page.askLike();
        } else if (name == "status") {
            page.accountReq = engine.request("account", {});
            page.likeId = "";       // asked again once the account has answered
            page.favId = "";
            page.askFavorite();
        }
    }

    function cycleRepeat() {
        engine.request("repeat", {
            "mode": player.repeat == "off" ? "all" : (player.repeat == "all" ? "one" : "off")
        });
    }

    // A track from the account's lists has no album or artist id: the engine asks YouTube Music.
    function lookup(what) {
        if (! player.track.id || infoReq != "") {
            return;
        }
        infoFor = what;
        infoReq = engine.request("info", {
            "id": player.track.id
        });
    }

    function viewAlbum() {
        if (! hostPane) {
            return;
        }
        if (! player.track.album_id) {
            lookup("album");
            return;
        }
        openAlbum(player.track.album_id);
    }

    function openAlbum(id) {
        var p = albumPage.createObject();
        p.navPane = hostPane;
        p.browseId = id;
        openBelow(p);
    }

    function viewArtist() {
        if (! hostPane) {
            return;
        }
        if (! player.track.artist_id) {
            lookup("artist");
            return;
        }
        openArtist(player.track.artist_id);
    }

    function openArtist(id) {
        var p = shelfPage.createObject();
        p.navPane = hostPane;
        p.browseId = id;
        openBelow(p);
    }

    function showQueue() {
        if (hostPane) {
            var p = queuePage.createObject();
            p.navPane = hostPane;
            p.pushed = true;
            openBelow(p);
        }
    }

    // Takes the screen: the tabs go at once and the player's content rises from the bottom as it
    // fades in (the user, 2026-11-21: "growing to fill the screen", "an opacity animation").
    function present() {
        backScene = Application.scene;
        Application.scene = page;
        riseIn.play();
    }

    // Close: the content sinks and fades, then the tabs are back.
    function leave() {
        if (! leaving) {
            leaving = true;
            page.actionBarVisibility = ChromeVisibility.Hidden;
            sinkOut.play();
        }
    }

    // An album, an artist or the queue: this page leaves first, then that one is pushed on the tab.
    // Not at once: giving the scene back from inside an action's own handler, with a page pushed
    // on a pane that was off the screen, closed the app (1.0.1.65, Queue).
    function openBelow(p) {
        pendingPage = p;
        leave();
    }

    // The end of the sinking: the tabs are back, and this page is done with.
    function leaveNow() {
        if (backScene) {
            Application.scene = backScene;
            backScene = undefined;
            if (underPage) {
                if (underPage.content) {
                    underPage.content.opacity = 1;
                }
                underPage.actionBarVisibility = ChromeVisibility.Default;
            }
            if (pendingPage && hostPane) {
                hostPane.push(pendingPage);
            }
            cleanup();
            page.destroy();
        }
    }

    // Called when the page leaves the screen.
    // One try each: a disconnect that throws must not leave the others connected.
    function cleanup() {
        try {
            player.positionChanged.disconnect(page.sync);
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
        player.positionChanged.connect(page.sync);
        player.changed.connect(page.onPlayer);
        thumbs.ready.connect(page.onThumb);
        engine.reply.connect(page.onReply);
        engine.event.connect(page.onEvent);
        page.accountReq = engine.request("account", {});
        page.onPlayer();
    }


    // Letter shortcuts are safe here: no text field on this page.
    actions: [
        // Where a pushed page has Back: this page is the whole scene and has none.
        ActionItem {
            title: qsTr("Close")
            imageSource: "asset:///images/ic_expand_down.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            onTriggered: {
                page.leave();
            }
        },
        ActionItem {
            title: qsTr("Previous")
            imageSource: "asset:///images/ic_previous.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            shortcuts: Shortcut {
                key: "P"
            }
            onTriggered: {
                engine.request("prev", {});
            }
        },
        ActionItem {
            //: verbs, the button that pauses or starts playback
            title: player.playing ? qsTr("Pause") : qsTr("Play")
            imageSource: player.playing ? "asset:///images/ic_pause.png" : "asset:///images/ic_play.png"
            ActionBar.placement: ActionBarPlacement.Signature
            shortcuts: Shortcut {
                key: "Space"
            }
            onTriggered: {
                engine.request("toggle", {});
            }
        },
        ActionItem {
            title: qsTr("Next")
            imageSource: "asset:///images/ic_next.png"
            ActionBar.placement: ActionBarPlacement.OnBar
            shortcuts: Shortcut {
                key: "N"
            }
            onTriggered: {
                engine.request("next", {});
            }
        },
        ActionItem {
            title: qsTr("Queue")
            imageSource: "asset:///images/ic_queue.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            shortcuts: Shortcut {
                key: "Q"
            }
            onTriggered: {
                page.showQueue();
            }
        },
        ActionItem {
            title: player.track.kind == "episode" ? qsTr("View Podcast") : qsTr("View Album")
            imageSource: "asset:///images/ic_album.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            shortcuts: Shortcut {
                key: "A"
            }
            onTriggered: {
                page.viewAlbum();
            }
        },
        ActionItem {
            title: qsTr("View Artist")
            imageSource: "asset:///images/ic_artist.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                page.viewArtist();
            }
        },
        ActionItem {
            title: qsTr("Start Radio")
            imageSource: "asset:///images/ic_radio.png"
            enabled: player.track.kind != "episode"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                if (player.track.id) {
                    engine.request("radio", {});
                }
            }
        },
        ActionItem {
            title: page.favorite ? qsTr("Remove from Favorites") : qsTr("Add to Favorites")
            imageSource: "asset:///images/ic_favorite.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            shortcuts: Shortcut {
                key: "F"
            }
            onTriggered: {
                if (player.track.id) {
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
            }
        },
        ActionItem {
            // One action for both (the user, 2026-11-20): it says what a tap will do.
            title: page.liked ? qsTr("Remove Like on YouTube") : qsTr("Like on YouTube")
            imageSource: "asset:///images/ic_like.png"
            enabled: page.signedIn
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                if (player.track.id) {
                    page.likeSetReq = engine.request("like", {
                        "id": player.track.id,
                        "on": ! (page.liked && page.likeId == player.track.id)
                    });
                }
            }
        },
        InvokeActionItem {
            ActionBar.placement: ActionBarPlacement.InOverflow
            query {
                mimeType: "text/plain"
                invokeActionId: "bb.action.SHARE"
            }
            onTriggered: {
                data = "https://music.youtube.com/watch?v=" + player.track.id;
            }
        },
        ActionItem {
            title: player.eq == "off" || player.eq == "" ? qsTr("Equalizer (Off)") : qsTr("Equalizer (On)")
            imageSource: "asset:///images/ic_equalizer.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            onTriggered: {
                var sheet = eqSheet.createObject();
                if (sheet) {
                    sheet.show();
                }
            }
        },
        ActionItem {
            title: qsTr("Properties")
            imageSource: "asset:///images/ic_info.png"
            ActionBar.placement: ActionBarPlacement.InOverflow
            shortcuts: Shortcut {
                key: "I"
            }
            onTriggered: {
                info.show(player.track);
            }
        }
    ]

    Container {
        layout: StackLayout {
        }
        background: Color.Black
        animations: [
            ParallelAnimation {
                id: riseIn
                TranslateTransition {
                    fromY: ui.du(50)
                    toY: 0
                    duration: 350
                    easingCurve: StockCurve.CubicOut
                }
                FadeTransition {
                    fromOpacity: 0
                    toOpacity: 1
                    duration: 350
                }
                onEnded: {
                    page.actionBarVisibility = ChromeVisibility.Default;
                }
            },
            ParallelAnimation {
                id: sinkOut
                TranslateTransition {
                    toY: ui.du(50)
                    duration: 220
                    easingCurve: StockCurve.CubicIn
                }
                FadeTransition {
                    toOpacity: 0
                    duration: 220
                }
                onEnded: {
                    page.leaveNow();
                }
            }
        ]

        // Title and artist: 10 du on the Q10.
        Container {
            leftPadding: ui.du(2)
            rightPadding: ui.du(2)
            topPadding: ui.du(0.5)
            bottomPadding: ui.du(0.5)
            Label {
                text: player.title
                textStyle.fontSize: FontSize.Large
                textStyle.color: Color.White
                bottomMargin: 0
            }
            Label {
                text: player.state == "loading" ? qsTr("Loading...") : player.artist
                textStyle.fontSize: FontSize.Small
                textStyle.color: Color.create("#969696")
                topMargin: 0
            }
        }
        Container {
            horizontalAlignment: HorizontalAlignment.Fill
            preferredHeight: ui.px(2)
            background: Color.create("#323232")
        }
        // The album, and the two modes at the right as in the stock player.
        Container {
            leftPadding: ui.du(2)
            rightPadding: ui.du(1)
            preferredHeight: ui.du(9.5)
            layout: StackLayout {
                orientation: LayoutOrientation.LeftToRight
            }
            Label {
                text: player.album
                textStyle.fontSize: FontSize.Large
                textStyle.color: Color.White
                verticalAlignment: VerticalAlignment.Center
                layoutProperties: StackLayoutProperties {
                    spaceQuota: 1
                }
                gestureHandlers: TapHandler {
                    onTapped: {
                        page.viewAlbum();
                    }
                }
            }
            ImageButton {
                defaultImageSource: player.repeat == "one" ? "asset:///images/ic_repeat_one.png" : "asset:///images/ic_repeat.png"
                opacity: player.repeat == "off" ? 0.4 : 1.0
                preferredWidth: ui.du(8)
                preferredHeight: ui.du(8)
                verticalAlignment: VerticalAlignment.Center
                onClicked: {
                    page.cycleRepeat();
                }
            }
            // A toggle, as in the stock player (the user, 2026-11-14): on shuffles what is still to
            // come, off puts the queue back in its order.
            ImageButton {
                defaultImageSource: "asset:///images/ic_shuffle.png"
                opacity: player.shuffle ? 1.0 : 0.4
                preferredWidth: ui.du(8)
                preferredHeight: ui.du(8)
                verticalAlignment: VerticalAlignment.Center
                onClicked: {
                    engine.request("shuffle", {
                        "on": ! player.shuffle
                    });
                }
            }
        }

        // The artwork fills what is left; the scrubber lies over its lower edge.
        Container {
            layout: DockLayout {
            }
            background: Color.create("#262626")
            horizontalAlignment: HorizontalAlignment.Fill
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            ImageView {
                imageSource: page.artFile != "" ? page.artFile : "asset:///images/art_none_large.png"
                scalingMethod: ScalingMethod.AspectFill
                loadEffect: ImageViewLoadEffect.None
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
            }
            ActivityIndicator {
                running: player.state == "loading"
                visible: player.state == "loading"
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                preferredWidth: ui.du(12)
                preferredHeight: ui.du(12)
            }
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Bottom
                background: Color.create("#b0000000")
                leftPadding: ui.du(2)
                rightPadding: ui.du(2)
                topPadding: ui.du(1)
                bottomPadding: ui.du(1.5)
                Slider {
                    id: slider
                    fromValue: 0
                    toValue: Math.max(player.durationMs, 1)
                    // A seek is sent for a finger only. The value also changes when the page sets it and
                    // when toValue moves with the track's length: seeking on valueChanged restarted the
                    // song each time this page opened (0.1.0.4).
                    onTouch: {
                        if (event.touchType == TouchType.Down) {
                            page.dragging = true;
                            page.moved = false;
                        } else if (event.touchType == TouchType.Up || event.touchType == TouchType.Cancel) {
                            if (page.dragging && page.moved) {
                                page.holdUntil = new Date().getTime() + 800;
                                engine.request("seek", {
                                    "ms": Math.round(slider.immediateValue)
                                });
                            }
                            page.dragging = false;
                        }
                    }
                    onImmediateValueChanged: {
                        if (page.dragging && ! page.setting) {
                            page.moved = true;
                        }
                    }
                }
                Container {
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Label {
                        text: player.fmt(page.dragging ? slider.immediateValue : player.positionMs)
                        textStyle.fontSize: FontSize.XSmall
                        textStyle.color: Color.White
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                    }
                    // What is left, as the stock player counts it.
                    Label {
                        text: "-" + player.fmt(Math.max(0, player.durationMs - (page.dragging ? slider.immediateValue : player.positionMs)))
                        textStyle.fontSize: FontSize.XSmall
                        textStyle.color: Color.create("#969696")
                    }
                }
            }
        }
    }

    attachedObjects: [
        ComponentDefinition {
            id: albumPage
            source: "AlbumPage.qml"
        },
        ComponentDefinition {
            id: queuePage
            source: "QueuePage.qml"
        },
        ComponentDefinition {
            id: shelfPage
            source: "ShelfPage.qml"
        },
        ComponentDefinition {
            id: eqSheet
            source: "EqSheet.qml"
        },
        TrackInfo {
            id: info
        },
        SystemToast {
            id: notice
            autoUpdateEnabled: true
        }
    ]
}
