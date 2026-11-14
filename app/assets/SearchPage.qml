import bb.cascades 1.2
import bb.system 1.2
import bbmusic 1.0

// The search field (focused when the page shows), suggestions while typing, a filter, the results
// grouped by the engine's section titles, and the mini-player.
Page {
    id: page
    property variant navPane
    property string query: ""
    property string continuation: ""
    property string lastHeader: ""
    property string lastSet: ""        // the text we put in the field ourselves: not a keystroke
    property bool busy: false
    property bool noResults: false
    property bool showSuggestions: false
    // The ids of the requests whose answers this page wants.
    property string searchId: ""
    property string moreId: ""
    property string suggestId: ""

    function focusField() {
        focusTimer.stop();
        focusTimer.start();
    }

    function makeRow(item) {
        var playable = item.kind == "song" || item.kind == "video";
        return {
            "rowType": playable ? "playable" : "other",
            "title": item.title,
            "subtitle": item.subtitle ? item.subtitle : (item.artist ? item.artist : ""),
            "durationText": playable && item.duration_s > 0 ? player.fmt(item.duration_s * 1000) : "",
            "item": item
        };
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
                "title": "More"
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
            "filter": filterDrop.selectedValue
        });
        busy = searchId != "";
        if (! busy) {
            notice.body = "The engine is not connected.";
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
            if (ok) {
                resultModel.clear();
                lastHeader = "";
                addSections(result);
                noResults = resultModel.size() == 0;
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

    onCreationCompleted: {
        engine.reply.connect(page.handleReply);
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
                hintText: "Search"
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
            DropDown {
                id: filterDrop
                title: "Show"
                options: [
                    Option {
                        text: "All"
                        value: "all"
                        selected: true
                    },
                    Option {
                        text: "Songs"
                        value: "songs"
                    },
                    Option {
                        text: "Videos"
                        value: "videos"
                    },
                    Option {
                        text: "Albums"
                        value: "albums"
                    },
                    Option {
                        text: "Artists"
                        value: "artists"
                    },
                    Option {
                        text: "Playlists"
                        value: "playlists"
                    }
                ]
                onSelectedIndexChanged: {
                    if (page.query != "") {
                        page.runSearch(page.query);
                    }
                }
            }
        }
        ActivityIndicator {
            running: page.busy
            visible: page.busy
            horizontalAlignment: HorizontalAlignment.Center
            preferredWidth: ui.du(8)
            preferredHeight: ui.du(8)
        }
        Label {
            text: "No results."
            visible: page.noResults && ! page.busy && ! page.showSuggestions
            horizontalAlignment: HorizontalAlignment.Center
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
            visible: ! page.showSuggestions
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
                        contextActions: [
                            ActionSet {
                                title: ListItemData.title
                                ActionItem {
                                    title: "Play next"
                                    onTriggered: {
                                        song.ListItem.view.queueAdd(song.ListItem.indexPath, true);
                                    }
                                }
                                ActionItem {
                                    title: "Add to queue"
                                    onTriggered: {
                                        song.ListItem.view.queueAdd(song.ListItem.indexPath, false);
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
                if (entry.rowType == "playable") {
                    engine.request("play", {
                        "items": [entry.item],
                        "index": 0,
                        "radio": true
                    });
                } else if (entry.rowType == "more") {
                    page.searchMore();
                } else if (entry.rowType == "other") {
                    // Albums, artists and playlists come in a later step.
                    notice.body = "Not yet";
                    notice.show();
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
        }
    ]
}
