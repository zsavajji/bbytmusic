import bb.cascades 1.4
import "regions.js" as Regions

// Settings, from the application menu: the country of the content, and which marked parts of a
// track are skipped (SponsorBlock). All off, the default, means the service is never asked.
// Created when asked for, gone when closed; each choice is saved by the engine as it is made.
Sheet {
    id: sheet
    property string listReq: ""
    property string regionReq: ""
    property string gl: ""          // the country in use, as the engine last said or was told
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

    function onReply(id, ok, result, code, message) {
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
        listReq = engine.request("skip", {});
        regionReq = engine.request("region", {});
        sheet.open();
    }

    onClosed: {
        try {
            engine.reply.disconnect(sheet.onReply);
        } catch (e0) {
        }
        sheet.destroy();
    }

    Page {
        titleBar: TitleBar {
            title: "Settings"
            dismissAction: ActionItem {
                title: "Close"
                onTriggered: {
                    sheet.close();
                }
            }
        }
        ScrollView {
            Container {
                Header {
                    title: "Content country"
                }
                Container {
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    topPadding: ui.du(1)
                    bottomPadding: ui.du(2)
                    Label {
                        text: "The country YouTube Music picks songs and trends for. New releases and featured playlists still follow where you are."
                        multiline: true
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                        textStyle.color: Color.create("#969696")
                    }
                    DropDown {
                        id: region
                        title: "Country"
                        topMargin: ui.du(2)
                        onSelectedIndexChanged: {
                            sheet.sendRegion();
                        }
                    }
                }
                Header {
                    title: "Skip in a track"
                }
                Container {
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    topPadding: ui.du(1)
                    bottomPadding: ui.du(2)
                    Label {
                        text: "Parts that other listeners have marked, from SponsorBlock. While any of these is on, sponsor.ajay.app is asked about each track by a short hash of its id, never by the id."
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
                            text: "Non-music parts of a video"
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
                            text: "Sponsors"
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
                            text: "Intros"
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
                            text: "Outros"
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
            }
        }
        attachedObjects: [
            ComponentDefinition {
                id: optionDef
                Option {
                }
            }
        ]
    }
}
