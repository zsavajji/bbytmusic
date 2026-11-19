import bb.cascades 1.4

// "Properties", as the stock player calls it: what is known of a track, one fact per row.
Sheet {
    id: sheet

    function row(label, value) {
        if (value) {
            infoModel.append({
                "label": label,
                "value": "" + value
            });
        }
    }

    // track: an Item of docs/protocol.md.
    function show(track) {
        infoModel.clear();
        row(qsTr("Title"), track.title);
        row(qsTr("Artist"), track.artist);
        row(qsTr("Album"), track.album);
        row(qsTr("Year"), track.year);
        row(qsTr("Length"), track.duration_s > 0 ? player.fmt(track.duration_s * 1000) : "");
        row(qsTr("Plays"), track.plays);
        var kind = track.kind == "video" ? qsTr("Video") : (track.kind == "episode" ? qsTr("Episode") : qsTr("Song"));
        //: label of a row whose value says what kind of item it is (song, video, episode)
        row(qsTr("Type"), kind);
        row(qsTr("Explicit"), track.explicit ? qsTr("Yes") : "");
        row(qsTr("Link"), track.id ? "https://music.youtube.com/watch?v=" + track.id : "");
        sheet.open();
    }

    Page {
        titleBar: TitleBar {
            title: qsTr("Properties")
            dismissAction: ActionItem {
                title: qsTr("Close")
                onTriggered: {
                    sheet.close();
                }
            }
        }
        ListView {
            dataModel: ArrayDataModel {
                id: infoModel
            }
            listItemComponents: ListItemComponent {
                StandardListItem {
                    title: ListItemData.value
                    description: ListItemData.label
                }
            }
        }
    }
}
