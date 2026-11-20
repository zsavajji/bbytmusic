// Engine: construction, the worker thread, the source factory and the store.
#include "engine/engine.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ctime>

#include "engine/eq_sink.h"
#include "engine/item_json.h"
#include "engine/unplug_sink.h"
#include "innertube/region.h"
#include "store/history_store.h"
#include "store/queue_store.h"
#include "store/schema.h"
#include "store/settings.h"
#include "stream/cached_source.h"
#include "stream/resolve.h"
#include "util/log.h"

namespace bbm {

// Unlimited until start() reads the stored cap: the startup scan must not evict down to the default first.
Engine::Engine(EngineOptions o) : opt_(std::move(o)), cache_(opt_.data_dir + "/cache", INT64_MAX) {
    SourceFactory sources = opt_.sources;
    if (!sources) sources = [this](const Track &t, std::string &err) { return open_source(t, err); };
    player_ = std::make_unique<Player>(std::move(sources), [this]() -> std::unique_ptr<Sink> {
        std::unique_ptr<Sink> s = opt_.sinks ? opt_.sinks() : nullptr;
        if (!s) return nullptr;
        return std::make_unique<UnplugSink>(std::make_unique<EqSink>(std::move(s), eq_), [this] {
            log_info("headphones out: pausing");
            player_->pause();
        });
    });
}

Engine::~Engine() {
    closing_ = true;
    stop();
    if (account_) account_->cancel_sign_in();
    {
        std::lock_guard<std::mutex> lk(fav_mu_);
        fav_quit_ = true;
        if (fav_src_) fav_src_->cancel();
    }
    fav_cv_.notify_all();
    if (fav_thread_.joinable()) fav_thread_.join();
    {
        std::lock_guard<std::mutex> lk(wq_mu_);
        worker_quit_ = true;
    }
    wq_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    thumbs_.reset();   // after the worker, which asks it for covers; joins its fetch thread, whose ready() emits and posts
    player_->set_listener(nullptr);
    np_.stop();        // its reader thread calls the player
    player_.reset();   // joins the player's threads, and with them a listener call in flight
}

bool Engine::start(std::string &err) {
    if (!open_store(db_, opt_.data_dir + "/music.db", err)) {
        // The engine still plays without its store; it just forgets.
        log_error("store: " + err);
        err.clear();
    } else {
        db_ok_ = true;
    }
    {
        int64_t cap = opt_.cache_bytes;
        if (db_ok_) {
            const std::string s = get_setting(db_, "cache_mb", "");
            char *end = nullptr;
            const long long mb = std::strtoll(s.c_str(), &end, 10);
            if (!s.empty() && !*end && valid_cache_mb(mb)) cap = int64_t(mb) << 20;
        }
        cache_.set_cap(cap);
    }
    thumbs_ = std::make_unique<Thumbs>(opt_.data_dir + "/thumbs", [this](const std::string &url, int size, const std::string &file) {
        if (closing_) return;
        server_.emit("thumb", "\"url\":" + json_quote(url) + ",\"size\":" + std::to_string(size) + ",\"file\":" +
                                  json_quote(file));
        if (size == kArtSize) post([this, url] { np_art(url); });
    });
    np_art_.clean();
    {
        HttpSendFn send = opt_.account_http;
        if (!send) {
            auto http = std::make_shared<Http>();
            send = [http](const HttpRequest &r) { return http->send(r); };
        }
        account_ = std::make_unique<Account>(std::move(send));
        account_->on_change = [this] {
            if (closing_) return;
            server_.emit("account", account_json());
            post([this, stored = account_->stored()] {   // the store is the worker's alone
                std::string err;
                if (db_ok_ && !set_setting(db_, "account", stored, err)) log_warn("store: " + err);
            });
        };
    }
    player_->set_listener([this](const PlayerEvent &e) { on_event(e); });
    restore();
    add_commands();
    if (!server_.start(opt_.data_dir, err)) return false;
    worker_ = std::thread([this] { work(); });
    fav_thread_ = std::thread([this] { fav_download(); });
    return true;
}

void Engine::run() {
    server_.run();
    log_info("stopping");
    {
        std::lock_guard<std::mutex> lk(mu_);
        history_end();   // the track being played is left
    }
    const PlayerStatus s = player_->status();
    post([this, s] { save_state(s); });
    {
        std::lock_guard<std::mutex> lk(wq_mu_);
        worker_quit_ = true;   // after what is queued
    }
    wq_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void Engine::stop() { server_.stop(); }

void Engine::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lk(wq_mu_);
        jobs_.push_back(std::move(job));
    }
    wq_cv_.notify_all();
}

void Engine::work() {
    const auto tick = std::chrono::milliseconds(std::clamp<int64_t>(opt_.idle_exit_ms / 4, 10, 1000));
    std::unique_lock<std::mutex> lk(wq_mu_);
    for (;;) {
        wq_cv_.wait_for(lk, tick, [&] { return !jobs_.empty() || worker_quit_; });
        while (!jobs_.empty()) {
            std::function<void()> job = std::move(jobs_.front());
            jobs_.pop_front();
            lk.unlock();
            job();
            lk.lock();
        }
        if (worker_quit_) return;
        lk.unlock();
        if (idle_check()) {
            log_info("idle for " + std::to_string(opt_.idle_exit_ms / 1000) + " s: exiting");
            stop();
        }
        lk.lock();
    }
}

bool Engine::idle_check() {
    const PlayState st = player_->status().state;
    const bool idle = (st == PlayState::Stopped || st == PlayState::Paused) && server_.clients() == 0;
    const auto now = std::chrono::steady_clock::now();
    if (!idle) {
        idle_ = false;
        return false;
    }
    if (!idle_) {
        idle_ = true;
        idle_since_ = now;
    }
    return now - idle_since_ >= std::chrono::milliseconds(opt_.idle_exit_ms);
}

// The real sources: the segment cache in front of resolve() + make_source(), which run only for
// what the cache does not have.
std::unique_ptr<Source> Engine::open_source(const Track &t, std::string &err) {
    auto s = std::make_unique<CachedSource>(cache_, t.id, [id = t.id](StreamInfo &info, std::string &e) -> std::unique_ptr<Source> {
        if (!resolve(id, info, e)) return nullptr;
        std::unique_ptr<Source> src = make_source(info);
        if (!src) e = "no stream for " + id;
        return src;
    });
    if (!s->open(err)) return nullptr;
    return s;
}

Item Engine::item_for(const Track &t) {
    auto it = items_.find(t.id);
    return it != items_.end() ? it->second : from_track(t);
}

// The queue as it was at the last stop, paused where it was.
void Engine::restore() {
    if (!db_ok_) return;
    account_->load(get_setting(db_, "account", ""));
    fav_restore();
    if (int e = eq_preset_index(get_setting(db_, "eq", "off")); e >= 0) eq_.set_preset(e);
    if (Repeat r = Repeat::Off; parse_repeat(get_setting(db_, "repeat", "off"), r)) player_->set_repeat(r);
    if (std::string gl = get_setting(db_, "region", ""); valid_region(gl)) {   // before any command runs
        yt_.set_language("en", gl);
        region_known_ = true;
    }
    const bool shuffle_saved = get_setting(db_, "shuffle", "off") == "on";
    {
        std::vector<std::string> cats;
        std::string list = get_setting(db_, "skip", "");
        for (size_t i = 0; i <= list.size();) {
            size_t j = list.find(',', i);
            if (j == std::string::npos) j = list.size();
            const std::string name = list.substr(i, j - i);
            i = j + 1;
            if (std::find(skip_categories().begin(), skip_categories().end(), name) != skip_categories().end()) cats.push_back(name);
        }
        std::lock_guard<std::mutex> lk(mu_);
        skip_cats_ = std::move(cats);
    }
    std::vector<QueueRow> rows;
    QueueState st;
    std::string err;
    if (!load_queue(db_, rows, err) || !load_queue_state(db_, st, err)) {
        log_warn("restore: " + err);
        player_->restore_shuffle(shuffle_saved);
        return;
    }
    std::vector<Track> tracks;
    int index = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (const QueueRow &r : rows) {
            if (!r.known) continue;
            if (r.position == st.current) index = int(tracks.size());
            Item i = from_row(r.track);
            items_[i.id] = i;
            saved_tracks_.insert(i.id);
            tracks.push_back(to_track(i));
        }
    }
    if (tracks.empty()) {
        player_->restore_shuffle(shuffle_saved);
        return;
    }
    if (index >= int(tracks.size())) index = int(tracks.size()) - 1;
    log_info("restore: " + std::to_string(tracks.size()) + " tracks, at " + std::to_string(index) + ", " +
             std::to_string(st.position_ms) + " ms");
    player_->play(std::move(tracks), index, st.position_ms);
    player_->pause();
    // The mode goes on after the play, which would otherwise shuffle the queue that is already in its play order.
    player_->restore_shuffle(shuffle_saved);
    // The player thread takes the load at once (the network comes later, on the feed thread):
    // wait for it so that a status asked right away is already the restored one.
    for (int i = 0; i < 200 && player_->status().state == PlayState::Stopped; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void Engine::save_queue() {
    if (!db_ok_) return;
    std::vector<Item> items;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const std::vector<Track> q = player_->queue();
        std::map<std::string, Item> keep;
        for (const Track &t : q) {
            Item i = item_for(t);
            keep[t.id] = i;
            items.push_back(std::move(i));
        }
        items_.swap(keep);   // forget what left the queue
    }
    std::string err;
    std::vector<std::string> ids;
    for (const Item &i : items) {
        ids.push_back(i.id);
        if (saved_tracks_.count(i.id)) continue;
        if (upsert_track(db_, to_row(i), err)) saved_tracks_.insert(i.id);
        else log_warn("store: " + err);
    }
    if (!replace_queue(db_, ids, "", err)) log_warn("store: " + err);
    save_state(player_->status());
}

void Engine::save_state(const PlayerStatus &s) {
    std::string err;
    if (db_ok_ && !save_queue_state(db_, {s.index, s.position_ms}, err)) log_warn("store: " + err);
}

void Engine::history_end() {
    if (!hist_id_.empty() && hist_ms_ > 0 && db_ok_) {
        post([this, id = hist_id_, ms = hist_ms_] {
            std::string err;
            if (!add_history(db_, id, int64_t(std::time(nullptr)), ms, err)) log_warn("store: " + err);
        });
    }
    hist_id_.clear();
    hist_ms_ = 0;
}

}  // namespace bbm
