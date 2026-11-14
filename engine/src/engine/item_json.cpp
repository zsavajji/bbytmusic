#include "engine/item_json.h"

namespace bbm {

const char *state_name(PlayState s) {
    switch (s) {
    case PlayState::Loading: return "loading";
    case PlayState::Playing: return "playing";
    case PlayState::Paused: return "paused";
    default: return "stopped";
    }
}

std::string item_json(const Item &i) {
    auto s = [](const char *k, const std::string &v) { return std::string("\"") + k + "\":" + json_quote(v); };
    return "{" + s("kind", i.kind) + "," + s("id", i.id) + "," + s("title", i.title) + "," + s("subtitle", i.subtitle) +
           "," + s("thumb", i.thumb) + "," + s("artist", i.artist) + "," + s("album", i.album) + "," +
           s("artist_id", i.artist_id) + "," + s("album_id", i.album_id) + "," + s("playlist_id", i.playlist_id) +
           "," + s("params", i.params) + ",\"duration_s\":" + std::to_string(i.duration_s) +
           ",\"explicit\":" + (i.is_explicit ? "true" : "false") + "}";
}

std::string search_page_json(const SearchPage &p) {
    std::string out = "{\"sections\":[";
    for (size_t k = 0; k < p.sections.size(); k++) {
        if (k) out += ",";
        out += "{\"title\":" + json_quote(p.sections[k].title) + ",\"items\":[";
        for (size_t j = 0; j < p.sections[k].items.size(); j++) out += (j ? "," : "") + item_json(p.sections[k].items[j]);
        out += "]}";
    }
    return out + "],\"continuation\":" + json_quote(p.continuation) + "}";
}

bool playable(const Item &i) { return (i.kind == "song" || i.kind == "video") && !i.id.empty(); }

bool parse_items(Json array, std::vector<Item> &out, std::string &why) {
    out.clear();
    if (!array.is_array() || array.size() == 0) {
        why = "items: a non-empty array is required";
        return false;
    }
    for (Json j : array.items()) {
        Item i;
        i.kind = j["kind"].string();
        i.id = j["id"].string();
        i.title = j["title"].string();
        i.subtitle = j["subtitle"].string();
        i.thumb = j["thumb"].string();
        i.artist = j["artist"].string();
        i.album = j["album"].string();
        i.artist_id = j["artist_id"].string();
        i.album_id = j["album_id"].string();
        i.playlist_id = j["playlist_id"].string();
        i.params = j["params"].string();
        i.duration_s = int(j["duration_s"].i64());
        i.is_explicit = j["explicit"].boolean(j["is_explicit"].boolean());
        if (!playable(i)) {
            why = i.id.empty() ? "an item has no id"
                               : "only songs and videos can be played, not kind \"" + i.kind + "\" (" + i.id + ")";
            return false;
        }
        out.push_back(std::move(i));
    }
    return true;
}

Track to_track(const Item &i) { return Track{i.id, i.title, i.artist, i.album, i.thumb, i.duration_s}; }

TrackRow to_row(const Item &i) {
    TrackRow r;
    r.id = i.id;
    r.title = i.title;
    r.artist = i.artist;
    r.album = i.album;
    r.thumb = i.thumb;
    r.artist_id = i.artist_id;
    r.album_id = i.album_id;
    r.duration_s = i.duration_s;
    return r;
}

Item from_row(const TrackRow &r) {
    Item i;
    i.kind = "song";
    i.id = r.id;
    i.title = r.title;
    i.artist = r.artist;
    i.album = r.album;
    i.thumb = r.thumb;
    i.artist_id = r.artist_id;
    i.album_id = r.album_id;
    i.duration_s = int(r.duration_s);
    i.subtitle = r.album.empty() || r.artist.empty() ? r.artist + r.album : r.artist + " • " + r.album;
    return i;
}

Item from_track(const Track &t) {
    TrackRow r;
    r.id = t.id;
    r.title = t.title;
    r.artist = t.artist;
    r.album = t.album;
    r.thumb = t.thumb;
    r.duration_s = t.duration_s;
    return from_row(r);
}

}  // namespace bbm
