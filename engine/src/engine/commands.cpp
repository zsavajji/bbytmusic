// Engine: the commands of docs/protocol.md. Each runs on the reader thread of its connection.
#include <algorithm>
#include <future>

#include "engine/engine.h"
#include "engine/item_json.h"
#include "innertube/browse.h"
#include "innertube/next.h"
#include "innertube/region.h"
#include "innertube/search.h"
#include "store/history_store.h"
#include "store/settings.h"
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
        {"albums", SearchFilter::Albums}, {"artists", SearchFilter::Artists}, {"playlists", SearchFilter::Playlists},
        {"podcasts", SearchFilter::Podcasts}};
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
    if (!region_known_) detect_region();
    const bool r = call(yt_, err);
    if (!visitor_shared_ && !yt_.visitor_data().empty()) {
        set_visitor_data(yt_.visitor_data());
        visitor_shared_ = true;
    }
    return r;
}

void Engine::set_region(const std::string &gl) {
    region_known_ = true;
    yt_.set_language("en", gl);   // drops the visitor data too: the next call shares the new one with resolve()
    visitor_shared_ = false;
    post([this, gl] {   // the store is the worker's alone
        std::string err;
        if (db_ok_ && !set_setting(db_, "region", gl, err)) log_warn("store: " + err);
    });
}

// First start, no country stored: ask YouTube which it sees. One try per process, so a slow or
// failing answer costs one call; on failure "US" stays for this run and nothing is stored, so the
// next start asks again.
void Engine::detect_region() {
    region_known_ = true;
    std::string gl, err;
    if (!fetch_region(yt_, gl, err)) {
        log_warn("region: " + err + "; US for now");
        return;
    }
    log_info("region: " + gl);
    set_region(gl);
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
        if (!parse_filter(req["filter"], filter)) return bad("filter: all, songs, videos, albums, artists, playlists or podcasts");
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
    server_.on("radio", [this](Json) { return radio_command(); });
    simple("pause", [](Player &p) { p.pause(); });
    simple("stop", [](Player &p) { p.stop(); });
    simple("next", [](Player &p) { p.next(); });
    simple("prev", [](Player &p) { p.previous(); });
    server_.on("resume", [this](Json) { return resume(false); });
    server_.on("toggle", [this](Json) { return resume(true); });
    server_.on("seek", [this](Json req) {
        if (!req["ms"].is_number()) return bad("ms: a number is required");
        log_info("seek to " + std::to_string(req["ms"].i64()) + " ms");
        player_->seek(std::max<int64_t>(0, req["ms"].i64()));
        return ok();
    });
    server_.on("jump", [this](Json req) {
        int i;
        Reply err;
        if (!index_field(req, "index", int(player_->queue().size()), i, err)) return err;
        log_info("jump to " + std::to_string(i));
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
    server_.on("queue_clear", [this](Json) {
        player_->clear_others();
        return ok();
    });
    server_.on("queue_shuffle", [this](Json) {
        player_->shuffle();
        return ok();
    });
    server_.on("queue_sort", [this](Json req) {
        static const std::pair<const char *, QueueSort> keys[] = {
            {"title", QueueSort::Title}, {"artist", QueueSort::Artist},
            {"album", QueueSort::Album}, {"duration", QueueSort::Duration}};
        for (auto &[name, by] : keys)
            if (req["by"].str() == name) {
                if (player_->sort(by, req["desc"].boolean(false))) {   // the mode went off with it
                    server_.emit("mode", mode_json());
                    post([this] {
                        std::string err;
                        if (db_ok_ && !set_setting(db_, "shuffle", "off", err)) log_warn("store: " + err);
                    });
                }
                return ok();
            }
        return bad("by: title, artist, album or duration");
    });
    server_.on("browse", [this](Json req) {
        if (!req["id"].is_string() || req["id"].str().empty()) return bad("id: a browse id is required");
        if (!req["params"].is_null() && !req["params"].is_string()) return bad("params: a string");
        BrowsePage page;
        std::string err;
        const std::string id = req["id"].string(), params = req["params"].string();
        if (!yt_call([&](InnerTube &yt, std::string &e) { return browse(yt, id, page, e, params); }, err))
            return Reply::error("network", err);
        return ok(browse_page_json(page));
    });
    server_.on("browse_more", [this](Json req) {
        if (!req["continuation"].is_string() || req["continuation"].str().empty()) return bad("continuation is required");
        BrowsePage page;
        std::string err;
        const std::string c = req["continuation"].string();
        if (!yt_call([&](InnerTube &yt, std::string &e) { return browse_more(yt, c, page, e); }, err))
            return Reply::error("network", err);
        return ok(browse_more_json(page));
    });
    server_.on("history", [this](Json req) {
        // a limit that is not a number is the default
        const int n = int(std::clamp<int64_t>(req["limit"].is_number() ? req["limit"].i64() : 50, 1, 200));
        struct Answer {
            std::vector<PlayedTrack> rows;
            std::string err;
            bool ok = false;
        };
        auto answer = std::make_shared<Answer>();   // shared: the job may run after the wait gave up
        auto done = std::make_shared<std::promise<void>>();
        std::future<void> f = done->get_future();
        post([this, n, answer, done] {   // the store is the worker's alone
            answer->ok = db_ok_ && recent_tracks(db_, n, answer->rows, answer->err);
            done->set_value();
        });
        if (!db_ok_ || f.wait_for(std::chrono::seconds(3)) != std::future_status::ready)
            return Reply::error("internal", "the store is not available");
        if (!answer->ok) return Reply::error("internal", "the store: " + answer->err);
        std::string out = "{\"items\":[";
        for (size_t i = 0; i < answer->rows.size(); i++) out += (i ? "," : "") + played_json(answer->rows[i]);
        return ok(out + "]}");
    });
    server_.on("repeat", [this](Json req) {
        Repeat mode;
        if (!parse_repeat(req["mode"].str(), mode)) return bad("mode: off, all or one");
        player_->set_repeat(mode);
        server_.emit("mode", mode_json());
        post([this, mode] {   // the store is the worker's alone
            std::string err;
            if (db_ok_ && !set_setting(db_, "repeat", repeat_name(mode), err)) log_warn("store: " + err);
        });
        return ok();
    });
    server_.on("shuffle", [this](Json req) {
        if (!req["on"].is_bool()) return bad("on: a boolean is required");
        const bool on = req["on"].boolean(false);
        player_->set_shuffle(on);
        server_.emit("mode", mode_json());
        post([this, on] {
            std::string err;
            if (db_ok_ && !set_setting(db_, "shuffle", on ? "on" : "off", err)) log_warn("store: " + err);
        });
        return ok();
    });
    server_.on("eq", [this](Json req) {
        const auto &presets = eq_presets();
        if (!req["preset"].is_null()) {
            const int i = req["preset"].is_string() ? eq_preset_index(req["preset"].str()) : -1;
            if (i < 0) {
                std::string list;
                for (const EqPreset &p : presets) list += (list.empty() ? "" : ", ") + std::string(p.name);
                return bad("preset: one of " + list);
            }
            eq_.set_preset(i);
            server_.emit("mode", mode_json());
            post([this, i] {
                std::string err;
                if (db_ok_ && !set_setting(db_, "eq", eq_presets()[size_t(i)].name, err)) log_warn("store: " + err);
            });
        }
        std::string out = std::string("{\"preset\":\"") + presets[size_t(eq_.preset())].name + "\",\"presets\":[";
        for (size_t i = 0; i < presets.size(); i++) out += std::string(i ? ",\"" : "\"") + presets[i].name + "\"";
        return ok(out + "]}");
    });
    server_.on("region", [this](Json req) {
        if (!req["gl"].is_null() && !valid_region(req["gl"].str())) return bad("gl: two uppercase letters, like IT");
        std::lock_guard<std::mutex> lk(it_mu_);
        if (!req["gl"].is_null()) {
            set_region(req["gl"].string());
            server_.emit("region", "\"gl\":" + json_quote(yt_.gl()));
        }
        return ok("{\"gl\":" + json_quote(yt_.gl()) + "}");
    });
    server_.on("skip", [this](Json req) { return skip_command(req); });
    server_.on("thumbs", [this](Json req) {
        std::vector<std::string> urls;
        if (!req["urls"].is_array()) return bad("urls: an array of strings is required");
        for (Json u : req["urls"].items()) {
            if (!u.is_string()) return bad("urls: an array of strings is required");
            urls.push_back(u.string());
        }
        const int size = int(std::clamp<int64_t>(req["size"].i64(), 0, 1080));
        std::string out = "{\"files\":{";
        bool first = true;
        for (const auto &[url, file] : thumbs_->request(urls, size)) {
            out += (first ? "" : ",") + json_quote(url) + ":" + json_quote(file);
            first = false;
        }
        return ok(out + "}}");
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

std::string Engine::mode_json() {
    return std::string("\"repeat\":\"") + repeat_name(player_->repeat()) + "\",\"shuffle\":" +
           (player_->shuffle_mode() ? "true" : "false") + ",\"eq\":\"" + eq_presets()[size_t(eq_.preset())].name + "\"";
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
              ",\"error\":" + json_quote(s.error) + ",\"repeat\":\"" + repeat_name(player_->repeat()) + "\",\"shuffle\":" +
              (player_->shuffle_mode() ? "true" : "false") + ",\"eq\":\"" + eq_presets()[size_t(eq_.preset())].name + "\"}");
}

// Replaces the queue and starts. With radio and one song or video (an episode has none), its radio follows it:
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
    const bool radio = req["radio"].boolean(false) && items.size() == 1 && items[0].kind != "episode";
    log_info("play " + std::to_string(items.size()) + " item(s), first " + items[size_t(index)].id + " \"" +
             items[size_t(index)].title + "\"" + (radio ? ", with its radio" : ""));
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
    if (radio) radio_first(items[0].id, gen);
    return ok();
}

// The current track plays on and the rest of the queue becomes its radio; like play, the reply
// comes once the radio's first page is in.
Reply Engine::radio_command() {
    uint64_t gen;
    std::string id;
    {
        std::lock_guard<std::mutex> lk(mu_);
        gen = ++radio_gen_;
        radio_next_.clear();
        radio_asked_.clear();
        player_->clear_others();
        const std::vector<Track> q = player_->queue();
        if (q.empty()) return Reply::error("not_found", "the queue is empty");
        id = q[0].id;
    }
    log_info("radio of " + id);
    radio_first(id, gen);
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
