#include "innertube/browse.h"

#include <algorithm>

namespace bbm {

namespace {

bool parse_artist(Json response, const std::string &id, BrowsePage &out, std::string &err) {
    Json header = response["header"];
    Json h = header["musicImmersiveHeaderRenderer"];
    if (h.is_null()) h = header["musicVisualHeaderRenderer"];
    BrowsePage p;
    p.kind = "artist";
    p.id = id;
    p.title = runs_text(h["title"]);
    if (p.title.empty()) {
        err = "browse: no artist in the response";
        return false;
    }
    const std::string subs = runs_text(h["subscriptionButton"]["subscribeButtonRenderer"]["subscriberCountText"]);
    if (!subs.empty()) p.subtitle = subs + " subscribers";
    Json thumb = h["thumbnail"];
    if (thumb.is_null()) thumb = h["foregroundThumbnail"];
    p.thumb = pick_thumb(thumb);
    p.description = runs_text(h["description"]);
    bool top_done = false;
    for (Json c : response["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][0]["tabRenderer"]["content"]
                          ["sectionListRenderer"]["contents"].items()) {
        if (Json shelf = c["musicShelfRenderer"]; !shelf.is_null()) {
            if (top_done) continue;   // only the first shelf is the top songs
            top_done = true;
            for (Json r : shelf["contents"].items()) {
                Item it;
                if (!parse_item(r, it) || (it.kind != "song" && it.kind != "video")) continue;
                if (it.artist.empty()) {
                    it.artist = p.title;
                    it.artist_id = id;
                }
                p.items.push_back(std::move(it));
            }
        } else if (Json car = c["musicCarouselShelfRenderer"]; !car.is_null()) {
            SearchSection s;
            s.title = runs_text(car["header"]["musicCarouselShelfBasicHeaderRenderer"]["title"]);
            for (Json r : car["contents"].items()) {
                Item it;
                if (parse_item(r, it)) s.items.push_back(std::move(it));
            }
            if (!s.items.empty()) p.sections.push_back(std::move(s));
        }
    }
    out = std::move(p);
    return true;
}

}  // namespace

bool parse_browse(Json response, const std::string &id, BrowsePage &out, std::string &err) {
    out = BrowsePage{};
    if (id.rfind("UC", 0) == 0) return parse_artist(response, id, out, err);
    Json two = response["contents"]["twoColumnBrowseResultsRenderer"];
    Json h = two["tabs"][0]["tabRenderer"]["content"]["sectionListRenderer"]["contents"][0]
                ["musicResponsiveHeaderRenderer"];
    Json shelf;
    const bool album = id.rfind("MPRE", 0) == 0;
    for (Json c : two["secondaryContents"]["sectionListRenderer"]["contents"].items()) {
        shelf = album ? c["musicShelfRenderer"] : c["musicPlaylistShelfRenderer"];
        if (!shelf.is_null()) break;
    }
    if (h.is_null() || shelf.is_null()) {
        err = "browse: no tracks in the response";
        return false;
    }
    BrowsePage p;
    p.kind = album ? "album" : "playlist";
    p.id = id;
    p.title = runs_text(h["title"]);
    p.subtitle = runs_text(h["subtitle"]);
    p.second_subtitle = runs_text(h["secondSubtitle"]);
    for (Json r : h["subtitle"]["runs"].items()) {
        const std::string_view t = r["text"].str();
        if (t.size() == 4 && std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            p.year = std::string(t);
            break;
        }
    }
    p.thumb = pick_thumb(h["thumbnail"]);
    p.description = runs_text(h["description"]["musicDescriptionShelfRenderer"]["description"]);
    Json by = h["straplineTextOne"]["runs"][0];
    p.artist = by["text"].string();
    p.artist_id = by["navigationEndpoint"]["browseEndpoint"]["browseId"].string();
    if (p.artist.empty()) p.artist = h["facepile"]["avatarStackViewModel"]["text"]["content"].string();
    if (album) {
        for (Json b : h["buttons"].items())
            if (std::string pl = b["musicPlayButtonRenderer"]["playNavigationEndpoint"]["watchPlaylistEndpoint"]
                                  ["playlistId"].string();
                !pl.empty()) {
                p.playlist_id = pl;
                break;
            }
    } else {
        p.playlist_id = shelf["playlistId"].string();
    }
    for (Json c : shelf["contents"].items()) {
        Item it;
        if (!parse_item(c, it) || (it.kind != "song" && it.kind != "video")) continue;
        if (album) {
            it.album = p.title;
            it.album_id = id;
            if (it.thumb.empty()) it.thumb = p.thumb;
            if (it.artist.empty()) {
                it.artist = p.artist;
                it.artist_id = p.artist_id;
            }
            if (it.subtitle.empty()) it.subtitle = it.artist;
        }
        p.items.push_back(std::move(it));
    }
    if (p.items.empty()) {
        err = "browse: no tracks in the response";
        return false;
    }
    out = std::move(p);
    return true;
}

bool browse(InnerTube &yt, const std::string &id, BrowsePage &out, std::string &err) {
    JsonDoc doc;
    return yt.post("browse", "\"browseId\":" + json_quote(id), doc, err) && parse_browse(doc.root(), id, out, err);
}

}  // namespace bbm
