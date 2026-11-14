// Engine: the commands of docs/protocol.md. Each runs on the reader thread of its connection.
#include <algorithm>

#include "engine/engine.h"
#include "engine/item_json.h"
#include "innertube/next.h"
#include "innertube/search.h"
#include "stream/resolve.h"
#include "util/log.h"

namespace bbm {
namespace {

Reply ok(std::string result = "{}") {
    Reply r;
    r.result = std::move(result);
    return r;
}
Reply bad(std::string why) { return Reply::error("bad_request", std::move(why)); }

// A field that must hold an index into a list of n: bad_request if it is not a number,
// not_found if it is outside the list.
bool index_field(Json req, const char *name, int n, int &out, Reply &err) {
    const Json v = req[name];
    if (!v.is_number()) {
        err = bad(std::string(name) + ": a number is required");
        return false;
    }
    const int64_t i = v.i64();
    if (i < 0 || i >= n) {
        err = Reply::error("not_found", std::string(name) + " " + std::to_string(i) + ": the queue has " + std::to_string(n));
        return false;
    }
    out = int(i);
    return true;
}

bool parse_filter(Json v, SearchFilter &out) {
    static const std::pair<const char *, SearchFilter> names[] = {
        {"all", SearchFilter::All},       {"songs", SearchFilter::Songs},     {"videos", SearchFilter::Videos},
        {"albums", SearchFilter::Albums}, {"artists", SearchFilter::Artists}, {"playlists", SearchFilter::Playlists}};
    if (v.is_null()) {
        out = SearchFilter::All;
        return true;
    }
    for (auto &[name, f] : names)
        if (v.str() == name) {
            out = f;
            return true;
        }
    return false;
}

}  // namespace

bool Engine::yt_call(const std::function<bool(InnerTube &, std::string &)> &call, std::string &err) {
    std::lock_guard<std::mutex> lk(it_mu_);
    const bool r = call(yt_, err);
    if (!visitor_shared_ && !yt_.visitor_data().empty()) {
        set_visitor_data(yt_.visitor_data());
        visitor_shared_ = true;
    }
    return r;
}

void Engine::add_commands() {
    auto simple = [this](const char *cmd, std::function<void(Player &)> f) {
        server_.on(cmd, [this, f](Json) {
            f(*player_);
            return ok();
        });
    };
    server_.on("status", [this](Json) { return status(); });
    server_.on("search", [this](Json req) {
        SearchFilter filter;
        if (!req["q"].is_string() || req["q"].str().empty()) return bad("q: a query is required");
        if (!parse_filter(req["filter"], filter)) return bad("filter: all, songs, videos, albums, artists or playlists");
        SearchPage page;
        std::string err;
        const std::string q = req["q"].string();
        if (!yt_call([&](InnerTube &yt, std::string &e) { return search(yt, q, filter, page, e); }, err))
            return Reply::error("network", err);
        return ok(search_page_json(page));
    });
    server_.on("search_more", [this](Json req) {
        if (!req["continuation"].is_string() || req["continuation"].str().empty()) return bad("continuation is required");
        SearchPage page;
        std::string err;
        const std::string c = req["continuation"].string();
        if (!yt_call([&](InnerTube &yt, std::string &e) { return search_more(yt, c, page, e); }, err))
            return Reply::error("network", err);
        return ok(search_page_json(page));
    });
    server_.on("suggest", [this](Json req) {
        if (!req["q"].is_string()) return bad("q: a string is required");
        std::vector<std::string> list;
        std::string err;
        const std::string q = req["q"].string();
        if (!q.empty() && !yt_call([&](InnerTube &yt, std::string &e) { return suggestions(yt, q, list, e); }, err))
            return Reply::error("network", err);
        std::string out = "{\"suggestions\":[";
        for (size_t i = 0; i < list.size(); i++) out += (i ? "," : "") + json_quote(list[i]);
        return ok(out + "]}");
    });
    server_.on("play", [this](Json req) { return play(req); });
    simple("pause", [](Player &p) { p.pause(); });
    simple("stop", [](Player &p) { p.stop(); });
    simple("next", [](Player &p) { p.next(); });
    simple("prev", [](Player &p) { p.previous(); });
    server_.on("resume", [this](Json) { return resume(false); });
    server_.on("toggle", [this](Json) { return resume(true); });
    server_.on("seek", [this](Json req) {
        if (!req["ms"].is_number()) return bad("ms: a number is required");
        player_->seek(std::max<int64_t>(0, req["ms"].i64()));
        return ok();
    });
    server_.on("jump", [this](Json req) {
        int i;
        Reply err;
        if (!index_field(req, "index", int(player_->queue().size()), i, err)) return err;
        player_->jump(i);
        return ok();
    });
    server_.on("queue", [this](Json) {
        const PlayerStatus s = player_->status();
        const std::vector<Track> q = player_->queue();
        std::string out = "{\"items\":[";
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (size_t i = 0; i < q.size(); i++) out += (i ? "," : "") + item_json(item_for(q[i]));
        }
        return ok(out + "],\"index\":" + std::to_string(s.index) + "}");
    });
    server_.on("queue_add", [this](Json req) { return queue_add(req); });
    server_.on("queue_remove", [this](Json req) {
        int i;
        Reply err;
        if (!index_field(req, "index", int(player_->queue().size()), i, err)) return err;
        player_->remove(i);
        return ok();
    });
    server_.on("queue_move", [this](Json req) {
        const int n = int(player_->queue().size());
        int from, to;
        Reply err;
        if (!index_field(req, "from", n, from, err) || !index_field(req, "to", n, to, err)) return err;
        player_->move(from, to);   // `to` is the row's index once moved
        return ok();
    });
    server_.on("quit", [this](Json) {
        log_info("quit");
        // ponytail: a grace for the reply to leave before the connections close; a server-side
        // "close after this reply" would make it exact.
        post([this] {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            stop();
        });
        return ok();
    });
}

Reply Engine::status() {
    const PlayerStatus s = player_->status();
    const std::vector<Track> q = player_->queue();
    std::string track = "null";   // stopped keeps the last track, while the queue has it
    if (s.index >= 0 && size_t(s.index) < q.size()) {
        std::lock_guard<std::mutex> lk(mu_);
        track = item_json(item_for(q[size_t(s.index)]));
    }
    return ok("{\"version\":" + json_quote(opt_.version) + ",\"state\":\"" + state_name(s.state) + "\",\"index\":" +
              std::to_string(s.index) + ",\"track\":" + track + ",\"position_ms\":" + std::to_string(s.position_ms) +
              ",\"duration_ms\":" + std::to_string(s.duration_ms) + ",\"queue_len\":" + std::to_string(q.size()) +
              ",\"error\":" + json_quote(s.error) + "}");
}

// Replaces the queue and starts. With radio and one song or video, the song's radio follows it:
// its first page is fetched here, on the connection's thread, once the play has begun.
Reply Engine::play(Json req) {
    std::vector<Item> items;
    std::string why;
    if (!parse_items(req["items"], items, why)) return bad(why);
    int index = 0;
    if (!req["index"].is_null()) {
        Reply err;
        if (!index_field(req, "index", int(items.size()), index, err)) return bad(err.message);
    }
    const bool radio = req["radio"].boolean(false) && items.size() == 1;
    std::vector<Track> tracks;
    for (const Item &i : items) tracks.push_back(to_track(i));
    uint64_t gen;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (const Item &i : items) items_[i.id] = i;
        gen = ++radio_gen_;
        radio_next_.clear();
        radio_asked_.clear();
        player_->play(std::move(tracks), index, 0);
    }
    if (!radio) return ok();
    QueuePage page;
    std::string err;
    const std::string id = items[0].id;
    if (!yt_call([&](InnerTube &yt, std::string &e) { return watch_queue(yt, id, "", page, e); }, err)) {
        log_warn("radio of " + id + ": " + err);   // the song plays on without it
        return ok();
    }
    std::lock_guard<std::mutex> lk(mu_);
    if (gen == radio_gen_) {
        radio_next_ = page.continuation;
        radio_append(page.items);
    }
    return ok();
}

Reply Engine::queue_add(Json req) {
    std::vector<Item> items;
    std::string why;
    if (!parse_items(req["items"], items, why)) return bad(why);
    std::vector<Track> tracks;
    for (const Item &i : items) tracks.push_back(to_track(i));
    std::lock_guard<std::mutex> lk(mu_);
    for (const Item &i : items) items_[i.id] = i;
    player_->enqueue(std::move(tracks), req["next"].boolean(false));
    return ok();
}

// After a stop (or a restart) the queue is still there: resume plays its current track again.
Reply Engine::resume(bool toggle) {
    const PlayerStatus s = player_->status();
    const int n = int(player_->queue().size());
    if (s.state == PlayState::Stopped) {
        if (n > 0) player_->jump(std::clamp(s.index, 0, n - 1));
    } else if (toggle) {
        player_->toggle();
    } else {
        player_->resume();
    }
    return ok();
}

}  // namespace bbm
