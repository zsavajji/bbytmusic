import bb.cascades 1.4
import bb.system 1.2
import bbmusic 1.0

// The search field (focused when the page shows), suggestions while typing, the stock player's
// segmented control as the filter, and the results under a header per kind (Songs, Albums, ...),
// each row with its cover. A song plays with its radio; a podcast episode plays alone; an album,
// a playlist or a podcast show opens its page.
Page {
    id: page
    property variant navPane
    property string query: ""
    property string continuation: ""
    property string lastHeader: ""
    property string lastSet: ""        // the text we put in the field ourselves: not a keystroke
    property bool busy: false
    property bool noResults: false
    property bool failed: false
    // The engine is still waking, not down: the spinner, not the error.
    property bool waking: failed && ! engine.connected && ! engine.down
    property bool showSuggestions: false
    // The ids of the requests whose answers this page wants.
    property string searchId: ""
    property string moreId: ""
    property string suggestId: ""

    function focusField() {
        focusTimer.stop();
        focusTimer.start();
    }

    function art(url) {
        var f = url ? thumbs.file(url, 120) : "";
        return f != "" ? f : "asset:///images/art_none.png";
    }

    // What a row says under its title. A song or a video: artist, album, plays (its header
    // already says what it is). The others: YouTube's own line, "Album • Artist • 1998".
    function about(item, playable) {
        if (! playable || ! item.artist) {
            return item.subtitle ? item.subtitle : (item.artist ? item.artist : "");
        }
        var parts = [ item.artist ];
        if (item.album) {
            parts.push(item.album);
        }
        if (item.plays) {
            parts.push(item.plays);
        }
        return parts.join(" \u2022 ");
    }

    function makeRow(item) {
        var playable = item.kind == "song" || item.kind == "video";
        var episode = item.kind == "episode";
        return {
            "rowType": playable ? "playable" : (episode ? "episode" : "other"),
            "title": item.title,
            // A show: its publisher. An episode: YouTube's "Episode \u2022 Jan 27, 2025 \u2022 The Rest Is History".
            "subtitle": item.kind == "show" ? (item.artist ? item.artist : "") : about(item, playable),
            "durationText": (playable || episode) && item.duration_s > 0 ? player.fmt(item.duration_s * 1000) : "",
            "thumbUrl": item.thumb ? item.thumb : "",
            "image": art(item.thumb),
            "item": item
        };
    }

    function openBrowse(id) {
        var p = albumPage.createObject();
        p.navPane = navPane;
        p.browseId = id;
        navPane.push(p);
    }

    function open(item) {
        if (item.kind == "album" || item.kind == "playlist" || item.kind == "show") {
            openBrowse(item.id);      // a show's id is the MPSP... page of AlbumPage
        } else if (item.kind == "artist") {
            var p = shelfPage.createObject();
            p.navPane = navPane;
            p.browseId = item.id;
            navPane.push(p);
        } else {
            // A station: a later step.
            notice.body = qsTr("Not yet");
            notice.show();
        }
    }

    // An episode's album_id is its show's page.
    function viewAlbum(item) {
        if (item.album_id) {
            openBrowse(item.album_id);
        } else {
            notice.body = qsTr("This track has no album page.");
            notice.show();
        }
    }

    // ponytail: every row is looked at for each thumbnail that arrives; an index by url if a
    // long result list ever stutters.
    function onThumb(url, size, file) {
        if (size != 120) {
            return;
        }
        for (var i = 0; i < resultModel.size(); i ++) {
            var row = resultModel.value(i);
            if (row.thumbUrl == url && row.image != file) {
                row.image = file;
                resultModel.replace(i, row);
            }
        }
    }

    // Appends a search or search_more result; "More" goes last when there is a continuation.
    function addSections(result) {
        var rows = [];
        var sections = result.sections ? result.sections : [];
        for (var s = 0; s < sections.length; s ++) {
            var title = sections[s].title;
            if (title && title != lastHeader) {
                rows.push({
                    "rowType": "header",
                    "title": title
                });
                lastHeader = title;
            }
            var items = sections[s].items ? sections[s].items : [];
            for (var i = 0; i < items.length; i ++) {
                rows.push(makeRow(items[i]));
            }
        }
        continuation = result.continuation ? result.continuation : "";
        if (continuation != "") {
            rows.push({
                "rowType": "more",
                "title": qsTr("More")
            });
        }
        if (rows.length > 0) {
            resultModel.append(rows);
        }
    }

    function runSearch(text) {
        text = text.replace(/^\s+|\s+$/g, "");
        if (text == "") {
            return;
        }
        suggestTimer.stop();
        showSuggestions = false;
        suggestId = "";
        query = text;
        searchId = engine.request("search", {
            "q": text,
            "filter": filter.selectedValue
        });
        busy = searchId != "";
        failed = ! busy;
        if (! busy && engine.down) {
            notice.body = qsTr("The engine is not connected.");
            notice.show();
        }
    }

    function searchMore() {
        if (busy || continuation == "") {
            return;
        }
        moreId = engine.request("search_more", {
            "continuation": continuation
        });
        busy = moreId != "";
    }

    function scheduleSuggest(text) {
        if (text == lastSet) {
            return;
        }
        suggestTimer.stop();
        showSuggestions = text != "";
        if (text == "") {
            suggestModel.clear();
            return;
        }
        suggestTimer.start();
    }

    function handleReply(id, ok, result, code, message) {
        if (id == "") {
            return;
        }
        if (id == suggestId) {
            suggestId = "";
            if (ok && showSuggestions) {
                suggestModel.clear();
                var list = result.suggestions ? result.suggestions : [];
                for (var i = 0; i < list.length; i ++) {
                    suggestModel.append({
                        "title": list[i]
                    });
                }
            }
        } else if (id == searchId) {
            searchId = "";
            busy = false;
            failed = ! ok;      // main.qml shows why
            if (ok) {
                resultModel.clear();
                lastHeader = "";
                addSections(result);
                noResults = resultModel.size() == 0;
            } else {
                // Not the last query's rows again while the retry runs.
                resultModel.clear();
            }
        } else if (id == moreId) {
            moreId = "";
            busy = false;
            if (ok) {
                // The "More" row sits last; the new rows replace it.
                var last = resultModel.size() - 1;
                if (last >= 0 && resultModel.value(last).rowType == "more") {
                    resultModel.removeAt(last);
                }
                addSections(result);
            }
        }
    }

    // The engine is back: repeat a search that failed. (This page stays for the life of the app.)
    function onLink() {
        if (engine.connected && page.failed) {
            page.runSearch(page.query);
        }
    }

    onCreationCompleted: {
        engine.reply.connect(page.handleReply);
        engine.connectedChanged.connect(page.onLink);
        thumbs.ready.connect(page.onThumb);
        page.focusField();
    }

    Container {
        layout: StackLayout {
        }

        ConnectionBar {
        }
        Container {
            leftPadding: ui.du(1)
            rightPadding: ui.du(1)
            topPadding: ui.du(1)
            TextField {
                id: field
                hintText: qsTr("Search")
                clearButtonVisible: true
                input.submitKey: SubmitKey.Search
                input.flags: TextInputFlag.AutoCapitalizationOff | TextInputFlag.AutoCorrectionOff | TextInputFlag.SpellCheckOff
                onTextChanging: {
                    page.scheduleSuggest(text);
                }
                input.onSubmitted: {
                    page.runSearch(text);
                }
            }
        }
        // All first, then the most used (the stock player's own control). Albums, videos and
        // playlists show under All.
        SegmentedControl {
            id: filter
            Option {
                text: qsTr("All")
                value: "all"
                selected: true
            }
            Option {
                text: qsTr("Songs")
                value: "songs"
            }
            Option {
                text: qsTr("Artists")
                value: "artists"
            }
            Option {
                text: qsTr("Podcasts")
                value: "podcasts"
            }
            onSelectedValueChanged: {
                if (page.query != "") {
                    page.runSearch(page.query);
                }
            }
        }
        ActivityIndicator {
            running: page.busy || page.waking
            visible: page.busy || page.waking
            horizontalAlignment: HorizontalAlignment.Center
            preferredWidth: ui.du(8)
            preferredHeight: ui.du(8)
        }
        Label {
            text: qsTr("No results.")
            visible: page.noResults && ! page.busy && ! page.showSuggestions && ! page.failed
            horizontalAlignment: HorizontalAlignment.Center
        }
        Container {
            visible: page.failed && ! page.waking && ! page.showSuggestions
            horizontalAlignment: HorizontalAlignment.Center
            topPadding: ui.du(6)
            Label {
                text: qsTr("The search could not be done.")
                horizontalAlignment: HorizontalAlignment.Center
            }
            Button {
                text: qsTr("Retry")
                horizontalAlignment: HorizontalAlignment.Center
                onClicked: {
                    page.runSearch(page.query);
                }
            }
        }

        ListView {
            id: suggestList
            visible: page.showSuggestions
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            dataModel: ArrayDataModel {
                id: suggestModel
            }
            listItemComponents: ListItemComponent {
                StandardListItem {
                    title: ListItemData.title
                }
            }
            onTriggered: {
                var text = dataModel.data(indexPath).title;
                page.lastSet = text;
                field.text = text;
                page.runSearch(text);
            }
        }

        ListView {
            id: resultList
            visible: ! page.showSuggestions && ! page.failed
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            dataModel: ArrayDataModel {
                id: resultModel
            }

            // Called by the rows' context actions (list items cannot see this page's ids).
            function queueAdd(indexPath, next) {
                engine.request("queue_add", {
                    "items": [dataModel.data(indexPath).item],
                    "next": next
                });
            }
            function favorite(indexPath) {
                engine.request("favorite", {
                    "item": dataModel.data(indexPath).item,
                    "on": true
                });
            }
            function viewAlbum(indexPath) {
                page.viewAlbum(dataModel.data(indexPath).item);
            }

            function itemType(data, indexPath) {
                return data.rowType;
            }

            listItemComponents: [
                ListItemComponent {
                    type: "header"
                    Header {
                        title: ListItemData.title
                    }
                },
                ListItemComponent {
                    type: "playable"
                    StandardListItem {
                        id: song
                        title: ListItemData.title
                        description: ListItemData.subtitle
                        status: ListItemData.durationText
                        imageSource: ListItemData.image
                        contextActions: [
                            ActionSet {
                                title: ListItemData.title
                                subtitle: ListItemData.subtitle
                                ActionItem {
                                    //: puts the song right after the one playing
                                    title: qsTr("Play Next")
                                    imageSource: "asset:///images/ic_play_next.png"
                                    onTriggered: {
                                        song.ListItem.view.queueAdd(song.ListItem.indexPath, true);
                                    }
                                }
                                ActionItem {
                                    title: qsTr("Add to Queue")
                                    imageSource: "asset:///images/ic_add_queue.png"
                                    onTriggered: {
                                        song.ListItem.view.queueAdd(song.ListItem.indexPath, false);
                                    }
                                }
                                ActionItem {
                                    title: qsTr("Add to Favorites")
                                    imageSource: "asset:///images/ic_favorite.png"
                                    onTriggered: {
                                        song.ListItem.view.favorite(song.ListItem.indexPath);
                                    }
                                }
                                ActionItem {
                                    title: qsTr("View Album")
                                    imageSource: "asset:///images/ic_album.png"
                                    onTriggered: {
                                        song.ListItem.view.viewAlbum(song.ListItem.indexPath);
                                    }
                                }
                            }
                        ]
                    }
                },
                ListItemComponent {
                    type: "episode"
                    StandardListItem {
                        id: ep
                        title: ListItemData.title
                        description: ListItemData.subtitle
                        status: ListItemData.durationText
                        imageSource: ListItemData.image
                        contextActions: [
                            ActionSet {
                                title: ListItemData.title
                                subtitle: ListItemData.subtitle
                                ActionItem {
                                    title: qsTr("Play Next")
                                    imageSource: "asset:///images/ic_play_next.png"
                                    onTriggered: {
                                        ep.ListItem.view.queueAdd(ep.ListItem.indexPath, true);
                                    }
                                }
                                ActionItem {
                                    title: qsTr("Add to Queue")
                                    imageSource: "asset:///images/ic_add_queue.png"
                                    onTriggered: {
                                        ep.ListItem.view.queueAdd(ep.ListItem.indexPath, false);
                                    }
                                }
                                ActionItem {
                                    title: qsTr("Add to Favorites")
                                    imageSource: "asset:///images/ic_favorite.png"
                                    onTriggered: {
                                        ep.ListItem.view.favorite(ep.ListItem.indexPath);
                                    }
                                }
                                ActionItem {
                                    title: qsTr("View Podcast")
                                    imageSource: "asset:///images/ic_podcast.png"
                                    onTriggered: {
                                        ep.ListItem.view.viewAlbum(ep.ListItem.indexPath);
                                    }
                                }
                            }
                        ]
                    }
                },
                ListItemComponent {
                    type: "other"
                    StandardListItem {
                        title: ListItemData.title
                        description: ListItemData.subtitle
                        imageSource: ListItemData.image
                    }
                },
                ListItemComponent {
                    type: "more"
                    StandardListItem {
                        title: ListItemData.title
                    }
                }
            ]

            onTriggered: {
                var entry = dataModel.data(indexPath);
                if (entry.rowType == "playable" || entry.rowType == "episode") {
                    engine.request("play", {
                        "items": [entry.item],
                        "index": 0,
                        "radio": entry.rowType == "playable"      // an episode has no radio worth having
                    });
                } else if (entry.rowType == "more") {
                    page.searchMore();
                } else if (entry.rowType == "other") {
                    page.open(entry.item);
                }
            }
        }

        MiniPlayer {
            navPane: page.navPane
        }
    }

    attachedObjects: [
        Timer {
            id: suggestTimer
            singleShot: true
            interval: 300
            onTimeout: {
                page.suggestId = engine.request("suggest", {
                    "q": field.text
                });
            }
        },
        // requestFocus() straight from onCreationCompleted can come before the field is on screen.
        Timer {
            id: focusTimer
            singleShot: true
            interval: 250
            onTimeout: {
                field.requestFocus();
            }
        },
        SystemToast {
            id: notice
            autoUpdateEnabled: true
        },
        ComponentDefinition {
            id: albumPage
            source: "AlbumPage.qml"
        },
        ComponentDefinition {
            id: shelfPage
            source: "ShelfPage.qml"
        }
    ]
}
