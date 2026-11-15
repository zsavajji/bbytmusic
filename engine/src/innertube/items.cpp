#include "innertube/items.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <vector>

namespace bbm {
namespace {

constexpr std::string_view kSep = " \xE2\x80\xA2 ";   // " • "

bool ends_with(std::string_view t, std::string_view suffix) {
    return t.size() >= suffix.size() && t.substr(t.size() - suffix.size()) == suffix;
}

bool is_plays(std::string_view t) { return ends_with(t, " plays") || ends_with(t, " views"); }

bool is_year(std::string_view t) {
    return t.size() == 4 && std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool is_sep(std::string_view t) { return t == kSep || t == "\xE2\x80\xA2"; }

bool is_label(std::string_view t) {
    for (std::string_view l : {"Song", "Video", "Artist", "Album", "Single", "EP", "Playlist", "Episode",
                               "Podcast", "Profile"})
        if (t == l) return true;
    return false;
}

std::string page_type(Json endpoint) {   // of a browseEndpoint
    return endpoint["browseEndpointContextSupportedConfigs"]["browseEndpointContextMusicConfig"]["pageType"]
        .string();
}

bool has_explicit_badge(Json r) {
    for (const char *key : {"badges", "subtitleBadges"})
        for (Json b : r[key].items())
            if (b["musicInlineBadgeRenderer"]["icon"]["iconType"].str() == "MUSIC_EXPLICIT_BADGE") return true;
    return false;
}

// Reads the subtitle runs: artists, album, duration, and the subtitle text without the duration.
void read_subtitle(Json runs, Item &it) {
    std::vector<std::string> texts;
    std::vector<Json> rs;
    for (Json run : runs["runs"].items()) {
        texts.push_back(run["text"].string());
        rs.push_back(run);
    }
    if (!texts.empty() && parse_duration(texts.back()) > 0) {
        if (it.duration_s == 0) it.duration_s = parse_duration(texts.back());
        texts.pop_back();
        rs.pop_back();
        if (!texts.empty() && is_sep(texts.back())) { texts.pop_back(); rs.pop_back(); }
    }
    std::string plain_artist;
    for (size_t i = 0; i < texts.size(); ++i) {
        it.subtitle += texts[i];
        if (it.plays.empty() && is_plays(texts[i])) it.plays = texts[i];
        if (it.year.empty() && is_year(texts[i])) it.year = texts[i];
        Json browse = rs[i]["navigationEndpoint"]["browseEndpoint"];
        const std::string pt = page_type(browse);
        if (pt == "MUSIC_PAGE_TYPE_ARTIST" || pt == "MUSIC_PAGE_TYPE_USER_CHANNEL") {
            if (it.artist.empty()) it.artist_id = browse["browseId"].string();
            else it.artist += ", ";
            it.artist += texts[i];
        } else if (pt == "MUSIC_PAGE_TYPE_ALBUM") {
            it.album = texts[i];
            it.album_id = browse["browseId"].string();
        } else if (plain_artist.empty() && !is_sep(texts[i]) && !is_label(texts[i]) && !texts[i].empty() &&
                   !std::isdigit(static_cast<unsigned char>(texts[i][0])) && browse.is_null()) {
            plain_artist = texts[i];
        }
    }
    // "Song • Rick Astley" carries no links: take the first plain run as the artist.
    if (it.artist.empty() && (it.kind == "song" || it.kind == "video") && !plain_artist.empty())
        it.artist = plain_artist;
}

// Kind and ids from where the renderer points. `nav` is its navigationEndpoint (maybe null);
// `r` is searched for a play endpoint when nav has none.
bool classify(Json nav, Json r, Item &it) {
    Json browse = nav["browseEndpoint"];
    if (!browse.is_null()) {
        it.id = browse["browseId"].string();
        const std::string pt = page_type(browse);
        if (pt == "MUSIC_PAGE_TYPE_ALBUM" || pt == "MUSIC_PAGE_TYPE_AUDIOBOOK") {
            it.kind = "album";
            it.album_id = it.id;
            it.playlist_id = r["overlay"].find("watchPlaylistEndpoint")["playlistId"].string();
        } else if (pt == "MUSIC_PAGE_TYPE_ARTIST" || pt == "MUSIC_PAGE_TYPE_USER_CHANNEL") {
            it.kind = "artist";
            it.artist_id = it.id;
        } else if (pt == "MUSIC_PAGE_TYPE_PLAYLIST") {
            it.kind = "playlist";
            it.playlist_id = it.id.rfind("VL", 0) == 0 ? it.id.substr(2) : it.id;
        } else {
            return false;   // podcasts, genres, ...
        }
        return !it.id.empty();
    }
    Json watch = nav["watchEndpoint"];
    if (watch.is_null()) watch = r.find("watchEndpoint");
    if (!watch.is_null() && !watch["videoId"].str().empty()) {
        it.id = watch["videoId"].string();
        it.playlist_id = watch["playlistId"].string();
        it.params = watch["params"].string();
        const std::string vt = watch.find("musicVideoType").string();
        if (vt == "MUSIC_VIDEO_TYPE_ATV") it.kind = "song";
        else if (vt == "MUSIC_VIDEO_TYPE_PODCAST_EPISODE") return false;
        else it.kind = "video";
        return true;
    }
    Json wp = nav["watchPlaylistEndpoint"];
    if (wp.is_null()) wp = r["overlay"].find("watchPlaylistEndpoint");
    if (!wp.is_null() && !wp["playlistId"].str().empty()) {
        it.id = it.playlist_id = wp["playlistId"].string();
        it.params = wp["params"].string();
        it.kind = it.id.rfind("RD", 0) == 0 ? "station" : "playlist";
        return true;
    }
    return false;
}

bool finish(Json r, Json nav, Json title, Json sub, Json thumb_holder, Item &out) {
    Item it;
    if (!classify(nav, r, it)) return false;
    it.title = runs_text(title);
    it.thumb = pick_thumb(thumb_holder);
    it.is_explicit = has_explicit_badge(r);
    const int ls = static_cast<int>(r["lengthSeconds"].i64());
    if (ls > 0) it.duration_s = ls;
    read_subtitle(sub, it);
    if (it.kind == "artist" && it.artist.empty()) it.artist = it.title;
    out = std::move(it);
    return true;
}

bool parse_responsive(Json r, Item &out) {
    auto col = [&](size_t i) { return r["flexColumns"][i]["musicResponsiveListItemFlexColumnRenderer"]["text"]; };
    if (!finish(r, r["navigationEndpoint"], col(0), col(1), r["thumbnail"], out)) return false;
    if (out.plays.empty())
        if (std::string t = runs_text(col(2)); is_plays(t)) out.plays = t;
    if (out.duration_s == 0)
        out.duration_s = parse_duration(
            runs_text(r["fixedColumns"][0]["musicResponsiveListItemFixedColumnRenderer"]["text"]));
    if (out.album.empty())   // an artist's top songs carry their album in the fourth column
        for (Json run : col(3)["runs"].items()) {
            Json be = run["navigationEndpoint"]["browseEndpoint"];
            if (page_type(be) == "MUSIC_PAGE_TYPE_ALBUM") {
                out.album = run["text"].string();
                out.album_id = be["browseId"].string();
                break;
            }
        }
    return true;
}

bool parse_two_row(Json r, Item &out) {
    // subtitle is a runs object too; the explicit badge is in subtitleBadges.
    return finish(r, r["navigationEndpoint"], r["title"], r["subtitle"], r["thumbnailRenderer"], out);
}

bool parse_panel(Json r, Item &out) {
    Item it;
    Json sub = r["longBylineText"].is_null() ? r["shortBylineText"] : r["longBylineText"];
    if (!finish(r, r["navigationEndpoint"], r["title"], sub, r["thumbnail"], it)) return false;
    if (it.id.empty()) return false;
    if (it.duration_s == 0) it.duration_s = parse_duration(runs_text(r["lengthText"]));
    out = std::move(it);
    return true;
}

// "1 hr 46 min", "9 min 4 sec", "27 min" -> seconds; 0 for the empty text of a live stream.
int parse_spoken_duration(std::string_view t) {
    long total = 0, n = 0;
    bool digits = false;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] >= '0' && t[i] <= '9') {
            n = n * 10 + (t[i] - '0');
            digits = true;
        } else if (digits && t[i] != ' ') {   // the first letter of the unit; the rest of the word is skipped
            total += n * (t[i] == 'h' ? 3600 : t[i] == 'm' ? 60 : t[i] == 's' ? 1 : 0);
            n = 0;
            digits = false;
            while (i + 1 < t.size() && t[i + 1] != ' ') ++i;
        }
    }
    return static_cast<int>(total);
}

// A podcast episode: onTap plays it, the second title names its show (the show's own page has
// none: browse fills it in). The subtitle is YouTube's "148K views • Apr 16".
bool parse_episode(Json r, Item &out) {
    Json watch = r["onTap"]["watchEndpoint"];
    if (watch["videoId"].str().empty()) return false;
    Item it;
    it.kind = "episode";
    it.id = watch["videoId"].string();
    it.title = runs_text(r["title"]);
    it.thumb = pick_thumb(r["thumbnail"]);
    read_subtitle(r["subtitle"], it);
    it.duration_s = parse_spoken_duration(runs_text(r["playbackProgress"]["musicPlaybackProgressRenderer"]["durationText"]));
    Json show = r["secondTitle"]["runs"][0];
    it.artist = show["text"].string();
    it.album_id = show["navigationEndpoint"]["browseEndpoint"]["browseId"].string();
    out = std::move(it);
    return true;
}

bool parse_card(Json r, Item &out) {
    Json nav = r["title"]["runs"][0]["navigationEndpoint"];
    return finish(r, nav, r["title"], r["subtitle"], r["thumbnail"], out);
}

}  // namespace

std::string pick_thumb(Json holder) {
    std::string best, narrowest;
    int64_t best_w = -1, narrow_w = INT64_MAX;
    for (Json t : holder.find("thumbnails").items()) {
        const int64_t w = t["width"].i64();
        const std::string url = t["url"].string();
        if (url.empty()) continue;
        if (w <= 544 && w > best_w) { best_w = w; best = url; }
        if (w < narrow_w) { narrow_w = w; narrowest = url; }
    }
    return best.empty() ? narrowest : best;
}

std::string runs_text(Json o) {
    if (o["simpleText"].is_string()) return o["simpleText"].string();
    std::string s;
    for (Json run : o["runs"].items()) s += run["text"].str();
    return s;
}

int parse_duration(std::string_view t) {
    if (t.empty() || t.size() > 8) return 0;
    long total = 0, part = 0;
    int groups = 1;
    for (char c : t) {
        if (c >= '0' && c <= '9') part = part * 10 + (c - '0');
        else if (c == ':') { total = total * 60 + part; part = 0; ++groups; }
        else return 0;
    }
    if (groups < 2 || groups > 3) return 0;
    return static_cast<int>(total * 60 + part);
}

bool parse_item(Json p, Item &out) {
    if (Json r = p["musicResponsiveListItemRenderer"]; !r.is_null()) return parse_responsive(r, out);
    if (Json r = p["musicTwoRowItemRenderer"]; !r.is_null()) return parse_two_row(r, out);
    if (Json r = p["playlistPanelVideoRenderer"]; !r.is_null()) return parse_panel(r, out);
    if (Json r = p["playlistPanelVideoWrapperRenderer"]["primaryRenderer"]["playlistPanelVideoRenderer"];
        !r.is_null())
        return parse_panel(r, out);
    if (Json r = p["musicCardShelfRenderer"]; !r.is_null()) return parse_card(r, out);
    if (Json r = p["musicMultiRowListItemRenderer"]; !r.is_null()) return parse_episode(r, out);
    return false;
}

std::string to_json(const Item &i) {
    auto s = [](const char *k, const std::string &v) { return std::string("\"") + k + "\":" + json_quote(v); };
    return "{" + s("kind", i.kind) + "," + s("id", i.id) + "," + s("title", i.title) + "," +
           s("subtitle", i.subtitle) + "," + s("thumb", i.thumb) + "," + s("artist", i.artist) + "," +
           s("album", i.album) + "," + s("artist_id", i.artist_id) + "," + s("album_id", i.album_id) + "," +
           s("playlist_id", i.playlist_id) + "," + s("params", i.params) + "," + s("plays", i.plays) + "," +
           s("year", i.year) + ",\"duration_s\":" + std::to_string(i.duration_s) +
           ",\"is_explicit\":" + (i.is_explicit ? "true" : "false") + "}";
}

}  // namespace bbm
