// Engine: the signed-in account's library, Liked Music, private playlists and likes, through the TV client.
#include "engine/engine.h"
#include "engine/item_json.h"
#include "engine/oauth.h"
#include "innertube/browse.h"
#include "innertube/tv_browse.h"
#include "util/log.h"

namespace bbm {

bool library_id(const std::string &id) {
    for (const char *k : {"FEmusic_last_played", "FEmusic_liked_playlists", "FEmusic_liked_albums", "FEmusic_liked_videos",
                          "FEmusic_library_corpus_artists", "VLLM"})
        if (id == k) return true;
    return false;
}

bool Engine::tv_call(const std::string &host, const std::string &endpoint, const std::string &fields, JsonDoc &out, Reply &fail) {
    const std::string body = std::string("{\"context\":{\"client\":{\"clientName\":\"TVHTML5\",\"clientVersion\":\"") + kTvVersion +
                             "\",\"hl\":\"en\",\"gl\":\"US\"}}," + fields + "}";
    for (int attempt = 0;; attempt++) {
        std::string token, err;
        switch (account_->bearer(token, err)) {
        case OAuthResult::Ok: break;
        case OAuthResult::Refused: fail = Reply::error("signed_out", err); return false;
        case OAuthResult::Network: fail = Reply::error("network", err); return false;
        }
        HttpRequest req;
        req.method = "POST";
        req.url = "https://" + host + "/youtubei/v1/" + endpoint + "?prettyPrint=false";
        req.timeout_s = 30;
        req.headers = {{"User-Agent", kTvUserAgent},
                       {"Content-Type", "application/json"},
                       {"Authorization", "Bearer " + token},
                       {"Origin", "https://" + host},
                       {"X-YouTube-Client-Name", "7"},
                       {"X-YouTube-Client-Version", kTvVersion}};
        req.body = body;
        const HttpResponse r = account_->send(req);
        if (r.error.empty() && r.status == 401 && attempt == 0) {   // the token was stale: one refresh, one more try
            account_->invalidate();
            continue;
        }
        if (!r.error.empty()) {
            fail = Reply::error("network", endpoint + ": " + r.error);
            return false;
        }
        if (r.status < 200 || r.status >= 300) {
            fail = Reply::error("network", endpoint + ": HTTP " + std::to_string(r.status));
            return false;
        }
        std::string perr;
        if (!JsonDoc::parse(r.body, out, perr)) {
            fail = Reply::error("network", endpoint + ": bad JSON: " + perr);
            return false;
        }
        return true;
    }
}

namespace {
Reply done(std::string result) {
    Reply r;
    r.result = std::move(result);
    return r;
}
}  // namespace

Reply Engine::tv_browse(const std::string &id) {
    JsonDoc doc;
    Reply fail;
    if (!tv_call("music.youtube.com", "browse", "\"browseId\":" + json_quote(id), doc, fail)) return fail;
    TvPage tv;
    if (!parse_tv_page(doc.root(), tv)) return Reply::error("network", "browse: not a TV page");
    BrowsePage page;
    page.id = id;
    if (!tv.tabs.empty()) {
        page.kind = "feed";
        if (!tv.items.empty()) {
            SearchSection s;
            for (const TvTab &t : tv.tabs)
                if (t.selected) s.title = t.title;
            s.items = std::move(tv.items);
            page.sections.push_back(std::move(s));
        }
        if (!tv.continuation.empty()) page.continuation = "tvf:" + tv.continuation;
    } else {
        page.kind = "playlist";
        page.title = tv.title;
        page.subtitle = tv.subtitle;
        if (!tv.items.empty()) page.thumb = tv.items[0].thumb;
        page.playlist_id = id.compare(0, 2, "VL") == 0 ? id.substr(2) : id;
        for (Item &i : tv.items)
            if (playable(i)) page.items.push_back(std::move(i));
        if (!tv.continuation.empty()) page.continuation = "tvp:" + tv.continuation;
    }
    return done(browse_page_json(page));
}

Reply Engine::tv_browse_more(const std::string &token) {
    const bool feed = token.compare(0, 4, "tvf:") == 0;
    JsonDoc doc;
    Reply fail;
    if (!tv_call("music.youtube.com", "browse", "\"continuation\":" + json_quote(token.substr(4)), doc, fail)) return fail;
    TvPage tv;
    if (!parse_tv_page(doc.root(), tv)) return Reply::error("network", "browse: not a TV page");
    BrowsePage page;
    if (feed) {
        if (!tv.items.empty()) page.sections.push_back({"", std::move(tv.items)});
    } else {
        for (Item &i : tv.items)
            if (playable(i)) page.items.push_back(std::move(i));
    }
    if (!tv.continuation.empty()) page.continuation = (feed ? "tvf:" : "tvp:") + tv.continuation;
    return done(browse_more_json(page));
}

Reply Engine::like_command(Json req) {
    const std::string id = req["id"].string();
    if (id.empty() || id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
        return Reply::error("bad_request", "id: a video id is required");
    if (!req["on"].is_null() && !req["on"].is_bool()) return Reply::error("bad_request", "on: a boolean");
    JsonDoc doc;
    Reply fail;
    bool on;
    if (req["on"].is_null()) {   // a question: the watch page's like button says (LIKE, DISLIKE or INDIFFERENT)
        if (!tv_call("www.youtube.com", "next", "\"videoId\":" + json_quote(id), doc, fail)) return fail;
        on = doc.root().find("likeButtonRenderer")["likeStatus"].string() == "LIKE";
    } else {
        on = req["on"].boolean();
        if (!tv_call("www.youtube.com", on ? "like/like" : "like/removelike", "\"target\":{\"videoId\":" + json_quote(id) + "}", doc, fail))
            return fail;
        log_info("like " + id + (on ? " on" : " off"));
    }
    return done("{\"id\":" + json_quote(id) + ",\"on\":" + (on ? "true" : "false") + "}");
}

}  // namespace bbm
