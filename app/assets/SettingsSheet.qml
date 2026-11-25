import bb.cascades 1.4
import bb.system 1.2
import "regions.js" as Regions

// Settings, from the application menu: the country of the content, which marked parts of a
// track are skipped (SponsorBlock) and the space kept for songs. All skips off, the default,
// means the service is never asked.
// Created when asked for, gone when closed; each choice is saved by the engine as it is made.
Sheet {
    id: sheet
    property string listReq: ""
    property string regionReq: ""
    property string cacheReq: ""
    property string accountReq: ""
    // The last account answer or event, as plain strings: signed out until the engine has said.
    property string acctState: "out"
    property string acctName: ""
    property string acctSource: ""
    property string acctCode: ""
    property string acctUrl: ""
    property string acctError: ""
    property string tubeError: ""   // why Tube's card gave nothing ("" when it has not, or the user said no)
    property string gl: ""          // the country in use, as the engine last said or was told
    property int capMb: 0           // the cache's cap, as the engine last said (0: not yet)
    property bool filling: false    // the controls are being set from the engine's answers

    function has(list, name) {
        for (var i = 0; i < list.length; i ++) {
            if (list[i] == name) {
                return true;
            }
        }
        return false;
    }

    // The countries as options (made here: the list is long), and the one in use selected. The
    // DropDown's "changed" also fires for each option added and for the selection: not a wish.
    function fillRegions(inUse) {
        var list = Regions.list();
        var at = -1;
        sheet.filling = true;
        for (var i = 0; i < list.length; i ++) {
            var o = optionDef.createObject();
            o.text = list[i][1];
            o.value = list[i][0];
            region.add(o);
            if (list[i][0] == inUse) {
                at = i;
            }
        }
        if (at < 0) {
            // A country the list lacks: its bare code.
            var x = optionDef.createObject();
            x.text = inUse;
            x.value = inUse;
            region.add(x);
            at = list.length;
        }
        sheet.gl = inUse;
        region.selectedIndex = at;
        sheet.filling = false;
    }

    function sendRegion() {
        if (filling || region.selectedIndex < 0 || region.selectedValue == sheet.gl) {
            return;
        }
        sheet.gl = region.selectedValue;
        // Home reloads on the engine's `region` event.
        engine.request("region", {
            "gl": sheet.gl
        });
    }

    function setAccount(a) {
        acctState = a.state ? a.state : "out";
        acctName = a.name ? a.name : "";
        acctSource = a.source ? a.source : "";
        acctCode = a.code ? a.code : "";
        acctUrl = a.url ? a.url : "";
        acctError = a.error ? a.error : "";
    }

    function onEvent(name, fields) {
        if (name == "account") {
            sheet.setAccount(fields);
        }
    }

    function onTubeRefused(e) {
        sheet.tubeError = e;
    }

    function refuse(e) {
        if (e == "access_denied") {
            return qsTr("the request was refused");
        }
        if (e == "expired_token") {
            return qsTr("the code expired");
        }
        if (e == "not-signed-in") {
            return qsTr("Tube is not signed in to YouTube");
        }
        if (e == "no-card") {
            return qsTr("Tube's sign-in did not open");
        }
        if (e == "refused") {
            return qsTr("Google does not accept Tube's sign-in");
        }
        if (e == "network") {
            return qsTr("YouTube could not be reached");
        }
        return e;
    }

    function onReply(id, ok, result, code, message) {
        if (id != "" && id == sheet.accountReq) {
            sheet.accountReq = "";
            if (ok) {
                sheet.setAccount(result);
            }
        }
        if (id != "" && id == sheet.regionReq) {
            sheet.regionReq = "";
            if (ok && result.gl) {
                sheet.fillRegions(result.gl);
            }
        }
        if (id != "" && id == sheet.listReq) {
            sheet.listReq = "";
            if (ok) {
                var on = result.categories ? result.categories : [];
                // A switch's "changed" also fires when code moves it: not a wish of the user's.
                sheet.filling = true;
                music.checked = sheet.has(on, "music_offtopic");
                sponsor.checked = sheet.has(on, "sponsor");
                intro.checked = sheet.has(on, "intro");
                outro.checked = sheet.has(on, "outro");
                sheet.filling = false;
            }
        }
        if (id != "" && id == sheet.cacheReq) {
            sheet.cacheReq = "";
            if (ok) {
                sheet.capMb = result.cap_mb;
                var cap = result.cap_mb >= 1024 ? qsTr("%1 GB").arg(result.cap_mb / 1024) : qsTr("%1 MB").arg(result.cap_mb);
                cacheUse.text = qsTr("%1 used of %2").arg(qsTr("%1 MB").arg(Math.round(result.bytes / 1048576))).arg(cap);
                sheet.filling = true;
                for (var i = 0; i < cacheSize.count(); i ++) {
                    if (cacheSize.at(i).value == result.cap_mb) {
                        cacheSize.selectedIndex = i;
                    }
                }
                sheet.filling = false;
            }
        }
    }

    function send() {
        if (filling) {
            return;
        }
        var on = [];
        if (sponsor.checked) {
            on.push("sponsor");
        }
        if (intro.checked) {
            on.push("intro");
        }
        if (outro.checked) {
            on.push("outro");
        }
        if (music.checked) {
            on.push("music_offtopic");
        }
        engine.request("skip", {
            "categories": on
        });
    }

    function show() {
        engine.reply.connect(sheet.onReply);
        engine.event.connect(sheet.onEvent);
        tube.refused.connect(sheet.onTubeRefused);
        tube.check();
        accountReq = engine.request("account", {});
        listReq = engine.request("skip", {});
        regionReq = engine.request("region", {});
        cacheReq = engine.request("cache", {});
        sheet.open();
    }

    onClosed: {
        try {
            engine.reply.disconnect(sheet.onReply);
        } catch (e0) {
        }
        try {
            engine.event.disconnect(sheet.onEvent);
        } catch (e1) {
        }
        try {
            tube.refused.disconnect(sheet.onTubeRefused);
        } catch (e2) {
        }
        sheet.destroy();
    }

    Page {
        titleBar: TitleBar {
            title: qsTr("Settings")
            dismissAction: ActionItem {
                title: qsTr("Close")
                onTriggered: {
                    sheet.close();
                }
            }
        }
        ScrollView {
            Container {
                Header {
                    title: qsTr("YouTube account")
                }
                Container {
                    // Fill, here and on the two below: a container is only as wide as what it holds,
                    // and a button that fills a narrow one stays narrow (the user, 2026-11-20).
                    horizontalAlignment: HorizontalAlignment.Fill
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    topPadding: ui.du(1)
                    bottomPadding: ui.du(2)
                    Label {
                        text: qsTr("Sign in to see your playlists, liked songs and albums. Search, playback and Favorites work without an account.")
                        multiline: true
                        visible: sheet.acctState == "out"
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                        textStyle.color: Color.create("#969696")
                    }
                    Label {
                        text: qsTr("The sign-in did not complete: %1").arg(sheet.refuse(sheet.acctError != "" ? sheet.acctError : sheet.tubeError))
                        multiline: true
                        topMargin: ui.du(2)
                        visible: sheet.acctState == "out" && (sheet.acctError != "" || sheet.tubeError != "")
                    }
                    Button {
                        // One tap: the Tube app (1.17.0 and later) shares its sign-in through its card.
                        text: qsTr("Sign in with Tube")
                        topMargin: ui.du(2)
                        horizontalAlignment: HorizontalAlignment.Fill
                        visible: sheet.acctState == "out" && tube.available
                        enabled: ! tube.busy
                        onClicked: {
                            sheet.tubeError = "";
                            tube.ask();
                        }
                    }
                    Button {
                        text: qsTr("Sign in")
                        topMargin: ui.du(2)
                        horizontalAlignment: HorizontalAlignment.Fill
                        visible: sheet.acctState == "out"
                        enabled: ! tube.busy
                        onClicked: {
                            sheet.tubeError = "";
                            sheet.accountReq = engine.request("account", {
                                "sign_in": true
                            });
                        }
                    }
                    Container {
                        visible: sheet.acctState == "pending"
                        horizontalAlignment: HorizontalAlignment.Fill
                        Label {
                            text: qsTr("On a phone or a computer, open")
                            multiline: true
                        }
                        Label {
                            text: sheet.acctUrl != "" ? sheet.acctUrl : "google.com/device"
                            textStyle.fontSize: FontSize.Large
                        }
                        Label {
                            text: qsTr("and enter this code:")
                            multiline: true
                        }
                        Label {
                            text: sheet.acctCode
                            visible: sheet.acctCode != ""
                            textStyle.base: SystemDefaults.TextStyles.BigText
                        }
                        ActivityIndicator {
                            running: sheet.acctState == "pending" && sheet.acctCode == ""
                            visible: sheet.acctCode == ""
                            preferredWidth: ui.du(8)
                            preferredHeight: ui.du(8)
                        }
                        Label {
                            text: qsTr("This page signs in by itself once the code is entered. The code is valid for 30 minutes.")
                            multiline: true
                            topMargin: ui.du(2)
                            textStyle.base: SystemDefaults.TextStyles.SmallText
                            textStyle.color: Color.create("#969696")
                        }
                        Button {
                            text: qsTr("Cancel")
                            topMargin: ui.du(2)
                            horizontalAlignment: HorizontalAlignment.Fill
                            onClicked: {
                                sheet.accountReq = engine.request("account", {
                                    "cancel": true
                                });
                            }
                        }
                    }
                    Container {
                        visible: sheet.acctState == "in"
                        horizontalAlignment: HorizontalAlignment.Fill
                        Label {
                            text: sheet.acctName != "" ? sheet.acctName : qsTr("Signed in")
                        }
                        Label {
                            text: qsTr("Shared by the Tube app")
                            visible: sheet.acctSource == "tube"
                            textStyle.base: SystemDefaults.TextStyles.SmallText
                            textStyle.color: Color.create("#969696")
                        }
                        Button {
                            text: qsTr("Sign out")
                            color: Color.create("#CC0000")   // the theme's red: it ends the session (the user, 2026-11-20)
                            topMargin: ui.du(2)
                            horizontalAlignment: HorizontalAlignment.Fill
                            onClicked: {
                                signOutDialog.show();   // asks first (the user, 2026-11-20)
                            }
                        }
                    }
                }
                Header {
                    title: qsTr("Content country")
                }
                Container {
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    topPadding: ui.du(1)
                    bottomPadding: ui.du(2)
                    Label {
                        text: qsTr("The country YouTube Music picks songs and trends for. New releases and featured playlists still follow where you are.")
                        multiline: true
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                        textStyle.color: Color.create("#969696")
                    }
                    DropDown {
                        id: region
                        title: qsTr("Country")
                        topMargin: ui.du(2)
                        onSelectedIndexChanged: {
                            sheet.sendRegion();
                        }
                    }
                }
                Header {
                    title: qsTr("Skip in a track")
                }
                Container {
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    topPadding: ui.du(1)
                    bottomPadding: ui.du(2)
                    Label {
                        text: qsTr("Parts that other listeners have marked, from SponsorBlock. While any of these is on, sponsor.ajay.app is asked about each track by a short hash of its id, never by the id.")
                        multiline: true
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                        textStyle.color: Color.create("#969696")
                    }
                    Container {
                        topMargin: ui.du(2)
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        Label {
                            text: qsTr("Non-music parts of a video")
                            verticalAlignment: VerticalAlignment.Center
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                        }
                        ToggleButton {
                            id: music
                            verticalAlignment: VerticalAlignment.Center
                            onCheckedChanged: {
                                sheet.send();
                            }
                        }
                    }
                    Container {
                        topMargin: ui.du(2)
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        Label {
                            text: qsTr("Sponsors")
                            verticalAlignment: VerticalAlignment.Center
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                        }
                        ToggleButton {
                            id: sponsor
                            verticalAlignment: VerticalAlignment.Center
                            onCheckedChanged: {
                                sheet.send();
                            }
                        }
                    }
                    Container {
                        topMargin: ui.du(2)
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        Label {
                            text: qsTr("Intros")
                            verticalAlignment: VerticalAlignment.Center
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                        }
                        ToggleButton {
                            id: intro
                            verticalAlignment: VerticalAlignment.Center
                            onCheckedChanged: {
                                sheet.send();
                            }
                        }
                    }
                    Container {
                        topMargin: ui.du(2)
                        layout: StackLayout {
                            orientation: LayoutOrientation.LeftToRight
                        }
                        Label {
                            text: qsTr("Outros")
                            verticalAlignment: VerticalAlignment.Center
                            layoutProperties: StackLayoutProperties {
                                spaceQuota: 1
                            }
                        }
                        ToggleButton {
                            id: outro
                            verticalAlignment: VerticalAlignment.Center
                            onCheckedChanged: {
                                sheet.send();
                            }
                        }
                    }
                }
                Header {
                    title: qsTr("Storage")
                }
                Container {
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    topPadding: ui.du(1)
                    bottomPadding: ui.du(2)
                    Label {
                        text: qsTr("Songs are kept on the phone after they play, so they play again without the network. The least recently played go first when the space is full.")
                        multiline: true
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                        textStyle.color: Color.create("#969696")
                    }
                    Label {
                        text: qsTr("Favorites are downloaded and never removed to make room; they count toward this space.")
                        multiline: true
                        topMargin: ui.du(1)
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                        textStyle.color: Color.create("#969696")
                    }
                    Label {
                        id: cacheUse
                        topMargin: ui.du(2)
                    }
                    DropDown {
                        id: cacheSize
                        title: qsTr("Space for songs")
                        topMargin: ui.du(2)
                        Option {
                            text: qsTr("%1 MB").arg(128)
                            value: 128
                        }
                        Option {
                            text: qsTr("%1 MB").arg(256)
                            value: 256
                        }
                        Option {
                            text: qsTr("%1 MB").arg(512)
                            value: 512
                        }
                        Option {
                            text: qsTr("%1 GB").arg(1)
                            value: 1024
                        }
                        Option {
                            text: qsTr("%1 GB").arg(2)
                            value: 2048
                        }
                        // The Options being added also fire "changed", before the engine has said what is in use.
                        onSelectedIndexChanged: {
                            if (sheet.filling || sheet.capMb == 0 || cacheSize.selectedIndex < 0 || cacheSize.selectedValue == sheet.capMb) {
                                return;
                            }
                            sheet.cacheReq = engine.request("cache", {
                                "cap_mb": cacheSize.selectedValue
                            });
                        }
                    }
                    Button {
                        text: qsTr("Clear cache")
                        topMargin: ui.du(2)
                        horizontalAlignment: HorizontalAlignment.Fill
                        onClicked: {
                            sheet.cacheReq = engine.request("cache", {
                                "clear": true
                            });
                        }
                    }
                }
            }
        }
        attachedObjects: [
            SystemDialog {
                id: signOutDialog
                title: qsTr("Sign out")
                body: qsTr("Sign out of YouTube on this phone? Your Favorites stay.")
                confirmButton.label: qsTr("Sign out")
                cancelButton.label: qsTr("Cancel")
                onFinished: {
                    if (value == SystemUiResult.ConfirmButtonSelection) {
                        sheet.accountReq = engine.request("account", {
                            "sign_out": true
                        });
                    }
                }
            },
            ComponentDefinition {
                id: optionDef
                Option {
                }
            }
        ]
    }
}
