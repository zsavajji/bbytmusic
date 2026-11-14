#include "innertube/browse.h"

#include <algorithm>

namespace bbm {

namespace {

// A carousel shelf as a section; empty when it holds no item parse_item takes.
SearchSection carousel_section(Json car) {
    SearchSection s;
    s.title = runs_text(car["header"]["musicCarouselShelfBasicHeaderRenderer"]["title"]);
    for (Json r : car["contents"].items()) {
        Item it;
        if (parse_item(r, it)) s.items.push_back(std::move(it));
    }
    return s;
}

// The shelves of a feed's section list, and the token of the next ones.
void read_feed(Json list, BrowsePage &p) {
    for (Json c : list["contents"].items())
        if (Json car = c["musicCarouselShelfRenderer"]; !car.is_null())
            if (SearchSection s = carousel_section(car); !s.items.empty()) p.sections.push_back(std::move(s));
    p.continuation = list["continuations"][0]["nextContinuationData"]["continuation"].string();
}

// The token of the trailing continuationItemRenderer of a playlist's rows ("" when the list ends).
std::string rows_continuation(Json rows) {
    for (Json r : rows.items())
        if (Json t = r["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"]; t.is_string())
            return t.string();
    return "";
}

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
            SearchSection s = carousel_section(car);
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
    if (id.rfind("FE", 0) == 0) {
        Json list = response["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][0]["tabRenderer"]["content"]
                            ["sectionListRenderer"];
        if (list.is_null()) {
            err = "browse: no feed in the response";
            return false;
        }
        out.kind = "feed";
        out.id = id;
        read_feed(list, out);
        return true;
    }
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
    if (!album) p.continuation = rows_continuation(shelf["contents"]);
    if (p.items.empty()) {
        err = "browse: no tracks in the response";
        return false;
    }
    out = std::move(p);
    return true;
}

std::string browse_id(const std::string &id) {
    for (const char *known : {"MPRE", "VL", "UC", "FE"})
        if (id.rfind(known, 0) == 0) return id;
    return "VL" + id;
}

bool browse(InnerTube &yt, const std::string &id, BrowsePage &out, std::string &err) {
    const std::string page = browse_id(id);
    JsonDoc doc;
    return yt.post("browse", "\"browseId\":" + json_quote(page), doc, err) && parse_browse(doc.root(), page, out, err);
}

bool parse_browse_more(Json response, BrowsePage &out, std::string &err) {
    out = BrowsePage{};
    Json list = response["continuationContents"]["sectionListContinuation"];
    if (!list.is_null()) {
        read_feed(list, out);
        return true;
    }
    // a playlist's next tracks
    for (Json a : response["onResponseReceivedActions"].items()) {
        Json rows = a["appendContinuationItemsAction"]["continuationItems"];
        if (rows.is_null()) continue;
        for (Json r : rows.items()) {
            Item it;
            if (parse_item(r, it) && (it.kind == "song" || it.kind == "video")) out.items.push_back(std::move(it));
        }
        out.continuation = rows_continuation(rows);
        return true;
    }
    err = "browse: no continuation in the response";
    return false;
}

bool browse_more(InnerTube &yt, const std::string &continuation, BrowsePage &out, std::string &err) {
    JsonDoc doc;
    return yt.post("browse", "\"continuation\":" + json_quote(continuation), doc, err) &&
           parse_browse_more(doc.root(), out, err);
}

}  // namespace bbm
