#include "innertube/next.h"

namespace bbm {
namespace {

void read_panel(Json panel, QueuePage &out) {
    for (Json c : panel["contents"].items()) {
        Item it;
        if (parse_item(c, it)) out.items.push_back(std::move(it));
    }
    Json c = panel["continuations"][0];
    Json n = c["nextRadioContinuationData"];
    if (n.is_null()) n = c["nextContinuationData"];
    out.continuation = n["continuation"].string();
}

}  // namespace

bool parse_queue(Json response, QueuePage &out, std::string &err) {
    out = QueuePage{};
    Json tabs = response["contents"]["singleColumnMusicWatchNextResultsRenderer"]["tabbedRenderer"]
                        ["watchNextTabbedResultsRenderer"]["tabs"];
    if (Json p = response["continuationContents"]["playlistPanelContinuation"]; !p.is_null()) {
        read_panel(p, out);
    } else if (!tabs.is_null()) {
        read_panel(tabs[0]["tabRenderer"]["content"]["musicQueueRenderer"]["content"]["playlistPanelRenderer"],
                   out);
        for (Json t : tabs.items()) {
            Json b = t["tabRenderer"]["endpoint"]["browseEndpoint"];
            const std::string pt = b["browseEndpointContextSupportedConfigs"]["browseEndpointContextMusicConfig"]
                                    ["pageType"].string();
            if (pt == "MUSIC_PAGE_TYPE_TRACK_LYRICS") out.lyrics_browse_id = b["browseId"].string();
            else if (pt == "MUSIC_PAGE_TYPE_TRACK_RELATED") out.related_browse_id = b["browseId"].string();
        }
    } else {
        err = "next: no queue in the response";
        return false;
    }
    if (out.items.empty()) {
        err = "next: the queue is empty";
        return false;
    }
    return true;
}

bool watch_queue(InnerTube &yt, const std::string &video_id, const std::string &playlist_id, QueuePage &out,
                 std::string &err) {
    const std::string pl = playlist_id.empty() ? "RDAMVM" + video_id : playlist_id;
    JsonDoc doc;
    return yt.post("next",
                   "\"videoId\":" + json_quote(video_id) + ",\"playlistId\":" + json_quote(pl) +
                       ",\"isAudioOnly\":true",
                   doc, err) &&
           parse_queue(doc.root(), out, err);
}

bool watch_queue_more(InnerTube &yt, const std::string &continuation, QueuePage &out, std::string &err) {
    JsonDoc doc;
    return yt.post("next", "\"continuation\":" + json_quote(continuation) + ",\"isAudioOnly\":true", doc, err) &&
           parse_queue(doc.root(), out, err);
}

}  // namespace bbm
