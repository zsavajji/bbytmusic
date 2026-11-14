import bb.cascades 1.4

// The equalizer: the engine's presets, the one in use marked. A tap sets it; what is playing
// changes within a moment. Created by Now Playing when asked for, gone when closed.
Sheet {
    id: sheet
    property string listReq: ""
    property variant presets: []

    // "bass_boost" -> "Bass Boost"
    function pretty(name) {
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
                "status": names[i] == player.eq ? "On" : ""
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
            title: "Equalizer"
            dismissAction: ActionItem {
                title: "Close"
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
