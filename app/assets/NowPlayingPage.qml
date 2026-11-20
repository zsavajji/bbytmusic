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
    property variant sheet          // NowPlayingSheet, which shows this page
    property variant hostPane       // the tab under the sheet: albums, artists and the queue open there
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
        if (id == page.favReq) {
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

    function viewAlbum() {
        var id = player.track.album_id;
        if (! id || ! hostPane) {
            notice.body = qsTr("This track has no album page.");
            notice.show();
            return;
        }
        var p = albumPage.createObject();
        p.navPane = hostPane;
        p.browseId = id;
        hostPane.push(p);
        closeSheet();
    }

    function viewArtist() {
        var id = player.track.artist_id;
        if (! id || ! hostPane) {
            notice.body = qsTr("This artist has no page.");
            notice.show();
            return;
        }
        var p = shelfPage.createObject();
        p.navPane = hostPane;
        p.browseId = id;
        hostPane.push(p);
        closeSheet();
    }

    function showQueue() {
        if (hostPane) {
            var p = queuePage.createObject();
            p.navPane = hostPane;
            p.pushed = true;
            hostPane.push(p);
            closeSheet();
        }
    }

    function closeSheet() {
        if (sheet) {
            sheet.close();
        }
    }

    // Called by the sheet when it has closed.
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
        fadeIn.play();      // while the sheet slides up
    }

    // Back closes the sheet: the page under this one in the sheet's pane is empty.
    paneProperties: NavigationPaneProperties {
        backButton: ActionItem {
            onTriggered: {
                page.closeSheet();
            }
        }
    }

    // Letter shortcuts are safe here: no text field on this page.
    actions: [
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
            FadeTransition {
                id: fadeIn
                fromOpacity: 0
                toOpacity: 1
                duration: 400
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
