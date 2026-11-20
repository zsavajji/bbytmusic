// Engine: the favourites. The list lives in the store; each one's audio is downloaded into the song
// cache by a thread of its own, one track at a time, and the cache keeps it from being evicted.
#include <ctime>
#include <future>

#include "engine/engine.h"
#include "engine/item_json.h"
#include "store/favorites_store.h"
#include "util/log.h"

namespace bbm {
namespace {

Reply bad(std::string why) { return Reply::error("bad_request", std::move(why)); }

// One request item through parse_items(): it takes an array, so the object is written out again as one.
bool parse_one(Json item, Item &out, std::string &why) {
    if (!item.is_object()) {
        why = "item: an object is required";
        return false;
    }
    std::string text = "[{";
    for (auto [k, v] : item.members()) {
        if (!v.is_string() && !v.is_number() && !v.is_bool()) continue;
        if (text.size() > 2) text += ",";
        text += json_quote(k) + ":" +
                (v.is_string() ? json_quote(v.str()) : v.is_bool() ? (v.boolean() ? "true" : "false") : std::to_string(v.i64()));
    }
    JsonDoc doc;
    std::string e;
    std::vector<Item> items;
    if (!JsonDoc::parse(text + "}]", doc, e) || !parse_items(doc.root(), items, why)) {
        if (why.empty()) why = "item: " + e;
        return false;
    }
    out = std::move(items[0]);
    return true;
}

}  // namespace

bool Engine::on_store(std::function<void()> job) {
    if (!db_ok_) return false;
    auto done = std::make_shared<std::promise<void>>();   // shared: the job may run after the wait gave up
    std::future<void> f = done->get_future();
    post([job = std::move(job), done] {   // the store is the worker's alone
        job();
        done->set_value();
    });
    return f.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
}

void Engine::fav_event(const std::string &id, bool on, bool offline) {
    if (closing_) return;
    size_t n;
    {
        std::lock_guard<std::mutex> lk(mu_);
        n = fav_ids_.size();
    }
    server_.emit("favorite", "\"id\":" + json_quote(id) + ",\"on\":" + (on ? "true" : "false") + ",\"offline\":" +
                                 (offline ? "true" : "false") + ",\"count\":" + std::to_string(n));
}

Reply Engine::favorites_command() {
    struct Answer {
        std::vector<FavoriteRow> rows;
        std::string err;
        bool ok = false;
    };
    auto a = std::make_shared<Answer>();
    if (!on_store([this, a] { a->ok = list_favorites(db_, a->rows, a->err); })) return Reply::error("internal", "the store is not available");
    if (!a->ok) return Reply::error("internal", "the store: " + a->err);
    std::string out = "{\"items\":[";
    for (size_t i = 0; i < a->rows.size(); i++) {
        std::string j = item_json(from_row(a->rows[i].track));
        j.pop_back();   // the closing brace
        out += (i ? "," : "") + j + ",\"added_at\":" + std::to_string(a->rows[i].added_at) + ",\"offline\":" +
               (a->rows[i].offline ? "true" : "false") + "}";
    }
    Reply r;
    r.result = out + "]}";
    return r;
}

Reply Engine::favorite_command(Json req) {
    Item item;
    std::string id = req["id"].string(), why;
    const bool has_item = !req["item"].is_null();
    if (has_item) {
        if (!parse_one(req["item"], item, why)) return bad(why);
        id = item.id;
    }
    if (id.empty()) return bad("id or item is required");
    auto reply = [&](bool on) {
        Reply r;
        r.result = "{\"id\":" + json_quote(id) + ",\"on\":" + (on ? "true" : "false") + "}";
        return r;
    };
    if (!req["on"].is_bool()) {   // a question, not a change
        std::lock_guard<std::mutex> lk(mu_);
        return reply(fav_ids_.count(id) != 0);
    }
    const bool on = req["on"].boolean();
    if (on && !has_item) return bad("item: an item is required to add a favourite");
    auto err = std::make_shared<std::string>();
    auto stored = std::make_shared<bool>(false);
    const TrackRow row = on ? to_row(item) : TrackRow{};
    const int64_t now = int64_t(std::time(nullptr));
    if (!on_store([this, on, id, row, now, err, stored] {
            *stored = on ? add_favorite(db_, row, now, *err) : remove_favorite(db_, id, *err);
        }))
        return Reply::error("internal", "the store is not available");
    if (!*stored) return Reply::error("internal", "the store: " + *err);
    std::set<std::string> kept;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (on) {
            fav_ids_.insert(id);
        } else {
            fav_ids_.erase(id);
            fav_done_.erase(id);
        }
        kept = fav_ids_;
    }
    cache_.set_kept(std::move(kept));
    if (on) fav_enqueue(id);
    fav_event(id, on, false);
    return reply(on);
}

// Start-up, before any thread runs: what the store has is kept, and what is not whole yet is downloaded.
void Engine::fav_restore() {
    std::vector<FavoriteRow> rows;
    std::string err;
    if (!list_favorites(db_, rows, err)) {
        log_warn("favorites: " + err);
        return;
    }
    std::set<std::string> kept;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (const FavoriteRow &r : rows) {
            fav_ids_.insert(r.track.id);
            if (r.offline) fav_done_.insert(r.track.id);
        }
        kept = fav_ids_;
    }
    cache_.set_kept(std::move(kept));
    for (const FavoriteRow &r : rows)
        if (!r.offline) fav_enqueue(r.track.id);
    log_info("favorites: " + std::to_string(rows.size()));
}

void Engine::fav_enqueue(const std::string &id) {
    {
        std::lock_guard<std::mutex> lk(fav_mu_);
        if (std::find(fav_queue_.begin(), fav_queue_.end(), id) != fav_queue_.end()) return;
        fav_queue_.push_back(id);
    }
    fav_cv_.notify_all();
}

void Engine::fav_requeue() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (const std::string &id : fav_ids_)
            if (!fav_done_.count(id)) ids.push_back(id);
    }
    for (const std::string &id : ids) fav_enqueue(id);
}

// ponytail: a failed download is retried only at the next engine start, after the favourite is added again or
// after a cap change; a network-is-back trigger if that proves too little.
void Engine::fav_download() {
    for (;;) {
        std::string id;
        {
            std::unique_lock<std::mutex> lk(fav_mu_);
            fav_cv_.wait(lk, [&] { return fav_quit_ || !fav_queue_.empty(); });
            if (fav_quit_) return;
            id = std::move(fav_queue_.front());
            fav_queue_.pop_front();
        }
        bool wanted;
        {
            std::lock_guard<std::mutex> lk(mu_);
            wanted = fav_ids_.count(id) != 0;
        }
        if (wanted) fav_fetch(id);
        std::unique_lock<std::mutex> lk(fav_mu_);
        if (fav_cv_.wait_for(lk, std::chrono::seconds(1), [&] { return fav_quit_; })) return;
    }
}

void Engine::fav_fetch(const std::string &id) {
    std::string why;
    int segments = 0;
    if (!cache_.complete(id)) {
        Track t;
        t.id = id;
        std::string err;
        std::unique_ptr<Source> src = opt_.sources ? opt_.sources(t, err) : open_source(t, err);
        if (!src) {
            why = "open: " + err;
        } else {
            {
                std::lock_guard<std::mutex> lk(fav_mu_);
                fav_src_ = src.get();
            }
            for (int i = 0, n = src->segment_count(); i < n; i++) {
                {
                    std::lock_guard<std::mutex> lk(fav_mu_);
                    if (fav_quit_) break;
                }
                Segment seg;
                if (!src->read(i, seg, err)) {
                    why = "read " + std::to_string(i) + ": " + err;
                    break;
                }
                segments++;
            }
            {
                std::lock_guard<std::mutex> lk(fav_mu_);
                fav_src_ = nullptr;
            }
            src.reset();   // unpins the track
        }
    }
    if (closing_) return;
    const bool offline = cache_.complete(id);
    if (!offline) {
        log_info("favorite " + id + " not offline: " + (why.empty() ? "does not fit in the cache" : why));
        return;
    }
    if (segments) log_info("favorite " + id + " downloaded, " + std::to_string(segments) + " segments");
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!fav_ids_.count(id)) return;   // removed meanwhile
        fav_done_.insert(id);
    }
    post([this, id] {
        std::string err;
        if (db_ok_ && !set_favorite_offline(db_, id, true, err)) log_warn("store: " + err);
    });
    fav_event(id, true, true);
}

}  // namespace bbm
