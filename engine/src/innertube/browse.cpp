#include "innertube/browse.h"

#include <algorithm>

namespace bbm {

bool parse_browse(Json response, const std::string &id, BrowsePage &out, std::string &err) {
    out = BrowsePage{};
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
