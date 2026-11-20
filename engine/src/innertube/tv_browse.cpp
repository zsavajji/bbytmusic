#include "innertube/tv_browse.h"

#include <algorithm>
#include <string_view>

namespace bbm {
namespace {

bool starts(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

// The texts of every line's items, separators included; a badge has no text and is skipped.
std::vector<std::string> line_texts(Json lines) {
    std::vector<std::string> v;
    for (Json line : lines.items())
        for (Json it : line["lineRenderer"]["items"].items())
            if (std::string t = runs_text(it["lineItemRenderer"]["text"]); !t.empty()) v.push_back(std::move(t));
    return v;
}

std::string join(const std::vector<std::string> &v) {
    std::string s;
    for (const std::string &t : v) s += (s.empty() ? "" : " ") + t;
    return s;
}

// Every tileRenderer beneath `j`, in document order, parsed; and the first nextContinuationData.
// A tile is not entered. YouTube varies the containers (grid, playlist list, shelves), so none is named.
void collect(Json j, std::vector<Item> &items, std::string &cont) {
    for (Json x : j.items()) collect(x, items, cont);
    for (auto [key, v] : j.members()) {
        if (key == "tileRenderer") {
            Item it;
            if (parse_tile(v, it)) items.push_back(std::move(it));
        } else if (key == "nextContinuationData") {
            if (cont.empty()) cont = v["continuation"].string();
        } else {
            collect(v, items, cont);
        }
    }
}

}  // namespace

bool parse_tile(Json tile, Item &out) {
    Json meta = tile["metadata"]["tileMetadataRenderer"];
    Json sel = tile["onSelectCommand"];
    const std::string type = tile["contentType"].string();
    const std::string browse_id = sel["browseEndpoint"]["browseId"].string();
    Item it;
    if (starts(browse_id, "MPRE")) {   // an album: not seen in any sample (the account has none), written from the id's shape alone
        it.kind = "album";
        it.id = browse_id;
    } else if (type == "TILE_CONTENT_TYPE_VIDEO") {
        Json w = sel["watchEndpoint"];   // a Short has a reelWatchEndpoint instead
        it.id = w["videoId"].string();
        if (it.id.empty()) return false;
        it.playlist_id = w["playlistId"].string();
        const std::string mt = w["watchEndpointMusicSupportedConfigs"]["watchEndpointMusicConfig"]["musicVideoType"].string();
        it.kind = mt == "MUSIC_VIDEO_TYPE_PODCAST_EPISODE" ? "episode" : mt == "MUSIC_VIDEO_TYPE_ATV" ? "song" : "video";
        it.artist = runs_text(meta["lines"][0]["lineRenderer"]["items"][0]["lineItemRenderer"]["text"]);
    } else if (type == "TILE_CONTENT_TYPE_PLAYLIST" && !browse_id.empty()) {
        it.kind = "playlist";
        it.id = browse_id;
        it.playlist_id = tile["contentId"].string();
    } else if (type == "TILE_CONTENT_TYPE_CHANNEL" && starts(browse_id, "UC")) {   // an FE... one links to a feed
        it.kind = "artist";
        it.id = browse_id;
    } else {
        return false;
    }
    Json header = tile["header"]["tileHeaderRenderer"];
    it.title = runs_text(meta["title"]);
    it.thumb = pick_thumb(header["thumbnail"]);
    it.subtitle = join(line_texts(meta["lines"]));
    for (Json o : header["thumbnailOverlays"].items()) {
        Json t = o["thumbnailOverlayTimeStatusRenderer"]["text"];
        if (!t.is_null()) { it.duration_s = parse_duration(runs_text(t)); break; }
    }
    out = std::move(it);
    return true;
}

bool parse_tv_page(Json root, TvPage &out) {
    Json contents = root["contents"], more = root["continuationContents"];
    if (!contents.is_object() && !more.is_object()) return false;
    TvPage p;
    if (more.is_object()) {
        collect(more, p.items, p.continuation);
        out = std::move(p);
        return true;
    }
    std::vector<Json> bodies;
    size_t sel = 0;
    for (Json sec : contents["tvBrowseRenderer"]["content"]["tvSecondaryNavRenderer"]["sections"].items())
        for (Json t : sec["tvSecondaryNavSectionRenderer"]["tabs"].items()) {
            Json tab = t["tabRenderer"];
            const bool on = tab["selected"].boolean();
            if (on && !std::any_of(p.tabs.begin(), p.tabs.end(), [](const TvTab &x) { return x.selected; })) sel = p.tabs.size();
            p.tabs.push_back({tab["endpoint"]["browseEndpoint"]["browseId"].string(), tab["title"].is_string() ? tab["title"].string() : runs_text(tab["title"]), on});
            bodies.push_back(tab["content"]);
        }
    if (!p.tabs.empty()) {
        // the other tabs hold only a reload continuation: the app asks for a tab by its browse id
        collect(bodies[sel], p.items, p.continuation);
    } else {
        Json meta = contents.find("entityMetadataRenderer");
        p.title = runs_text(meta["title"]);
        p.subtitle = join(line_texts(meta["bylines"]));
        collect(contents, p.items, p.continuation);
    }
    out = std::move(p);
    return true;
}

}  // namespace bbm
