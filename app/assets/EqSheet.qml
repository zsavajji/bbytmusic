import bb.cascades 1.4

// The equalizer: the engine's presets, the one in use marked. A tap sets it; what is playing
// changes within a moment. Created by Now Playing when asked for, gone when closed.
Sheet {
    id: sheet
    property string listReq: ""
    property variant presets: []

    // The engine's preset ids as the list shows them: the ones it ships, translated; any other
    // as its id read aloud ("bass_boost" -> "Bass Boost").
    function pretty(name) {
        if (name == "off") {
            return qsTr("Off");
        } else if (name == "bass_boost") {
            return qsTr("Bass Boost");
        } else if (name == "treble_boost") {
            return qsTr("Treble Boost");
        } else if (name == "vocal") {
            return qsTr("Vocal");
        } else if (name == "rock") {
            return qsTr("Rock");
        } else if (name == "pop") {
            return qsTr("Pop");
        } else if (name == "jazz") {
            return qsTr("Jazz");
        } else if (name == "classical") {
            return qsTr("Classical");
        } else if (name == "electronic") {
            return qsTr("Electronic");
        } else if (name == "loudness") {
            return qsTr("Loudness");
        }
        var words = name.split("_");
        for (var i = 0; i < words.length; i ++) {
            words[i] = words[i].charAt(0).toUpperCase() + words[i].substring(1);
        }
        return words.join(" ");
    }

    function fill() {
        var names = presets;
        var rows = [];
        for (var i = 0; i < names.length; i ++) {
            rows.push({
                "name": names[i],
                "title": pretty(names[i]),
                "status": names[i] == player.eq ? qsTr("On") : ""
            });
        }
        presetModel.clear();
        if (rows.length > 0) {
            presetModel.append(rows);
        }
    }

    function onReply(id, ok, result, code, message) {
        if (id != "" && id == sheet.listReq) {
            sheet.listReq = "";
            if (ok) {
                sheet.presets = result.presets ? result.presets : [];
                sheet.fill();
            }
        }
    }

    // The mode event has moved player.eq: move the mark.
    function onPlayer() {
        sheet.fill();
    }

    function show() {
        engine.reply.connect(sheet.onReply);
        player.changed.connect(sheet.onPlayer);
        listReq = engine.request("eq", {});
        sheet.open();
    }

    onClosed: {
        try {
            engine.reply.disconnect(sheet.onReply);
        } catch (e0) {
        }
        try {
            player.changed.disconnect(sheet.onPlayer);
        } catch (e1) {
        }
        sheet.destroy();
    }

    Page {
        titleBar: TitleBar {
            title: qsTr("Equalizer")
            dismissAction: ActionItem {
                title: qsTr("Close")
                onTriggered: {
                    sheet.close();
                }
            }
        }
        ListView {
            dataModel: ArrayDataModel {
                id: presetModel
            }
            listItemComponents: ListItemComponent {
                StandardListItem {
                    title: ListItemData.title
                    status: ListItemData.status
                }
            }
            onTriggered: {
                engine.request("eq", {
                    "preset": dataModel.data(indexPath).name
                });
            }
        }
    }
}
