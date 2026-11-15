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
        row("Title", track.title);
        row("Artist", track.artist);
        row("Album", track.album);
        row("Year", track.year);
        row("Length", track.duration_s > 0 ? player.fmt(track.duration_s * 1000) : "");
        row("Plays", track.plays);
        row("Type", track.kind == "video" ? "Video" : (track.kind == "episode" ? "Episode" : "Song"));
        row("Explicit", track.explicit ? "Yes" : "");
        row("Link", track.id ? "https://music.youtube.com/watch?v=" + track.id : "");
        sheet.open();
    }

    Page {
        titleBar: TitleBar {
            title: "Properties"
            dismissAction: ActionItem {
                title: "Close"
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
