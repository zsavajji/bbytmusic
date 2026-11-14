// Engine: SponsorBlock. The player thread notices a track and its position, the worker fetches
// the track's segments, and the player thread seeks past the one it is in.
#include <algorithm>

#include "engine/engine.h"
#include "store/settings.h"
#include "util/log.h"

namespace bbm {
namespace {
const auto kSkipMargin = 300;   // ms: too close to a segment's end to bother
const auto kSkipGuard = std::chrono::seconds(3);   // a skipped segment is left alone while the seek lands
}  // namespace

// mu_ held, on a Track event: the new track has no segments yet.
void Engine::skip_new_track(const std::string &id) {
    skip_track_ = id;
    skip_segs_.clear();
    skip_last_to_ = -1;
    if (!skip_cats_.empty()) skip_fetch(id, skip_cats_);
}

// Asks for the track's segments on the worker; they are kept only if the track and the
// categories are still the same when the answer comes.
void Engine::skip_fetch(const std::string &id, std::vector<std::string> cats) {
    post([this, id, cats] {
        std::vector<SkipSegment> segs;
        std::string err;
        const bool ok = opt_.segments ? opt_.segments(id, cats, segs, err) : fetch_segments(sponsor_http_, id, cats, segs, err);
        if (!ok) {
            log_warn("sponsorblock: " + err);
            return;
        }
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (skip_track_ != id || skip_cats_ != cats) return;
            skip_segs_ = std::move(segs);
        }
        skip_check(player_->status());   // a segment at the very start should not wait for the next position
    });
}

// If the position is inside a segment of the current track: skip it.
void Engine::skip_check(const PlayerStatus &s) {
    SkipSegment hit;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (s.track_id != skip_track_) return;
        const auto it = std::find_if(skip_segs_.begin(), skip_segs_.end(), [&](const SkipSegment &g) {
            return g.from_ms <= s.position_ms && s.position_ms < g.to_ms - kSkipMargin;
        });
        if (it == skip_segs_.end()) return;
        const auto now = std::chrono::steady_clock::now();
        if (it->to_ms == skip_last_to_ && now - skip_last_at_ < kSkipGuard) return;
        skip_last_to_ = it->to_ms;
        skip_last_at_ = now;
        hit = *it;
    }
    log_info("skip " + hit.category + " " + std::to_string(hit.from_ms) + ".." + std::to_string(hit.to_ms) + " ms of " + s.track_id);
    server_.emit("skipped", "\"category\":" + json_quote(hit.category) + ",\"from_ms\":" + std::to_string(hit.from_ms) +
                                ",\"to_ms\":" + std::to_string(hit.to_ms) + ",\"track_id\":" + json_quote(s.track_id));
    if (s.duration_ms > 0 && hit.to_ms >= s.duration_ms - 1000) player_->next();   // not a seek to the very end
    else player_->seek(hit.to_ms);
}

Reply Engine::skip_command(Json req) {
    const auto &all = skip_categories();
    if (!req["categories"].is_null()) {
        std::string allowed;
        for (const std::string &c : all) allowed += (allowed.empty() ? "" : ", ") + c;
        const std::string why = "categories: an array of " + allowed;
        if (!req["categories"].is_array()) return Reply::error("bad_request", why);
        std::set<std::string> want;
        for (Json c : req["categories"].items()) {
            if (!c.is_string() || std::find(all.begin(), all.end(), c.str()) == all.end()) return Reply::error("bad_request", why);
            want.insert(c.string());
        }
        std::vector<std::string> cats;
        for (const std::string &c : all)
            if (want.count(c)) cats.push_back(c);
        std::string joined;
        for (const std::string &c : cats) joined += (joined.empty() ? "" : ",") + c;
        {
            std::lock_guard<std::mutex> lk(mu_);
            skip_cats_ = cats;
            skip_segs_.clear();
            skip_last_to_ = -1;
            if (!cats.empty() && !skip_track_.empty() && player_->status().state != PlayState::Stopped) skip_fetch(skip_track_, cats);
        }
        post([this, joined] {   // the store is the worker's alone
            std::string err;
            if (db_ok_ && !set_setting(db_, "skip", joined, err)) log_warn("store: " + err);
        });
    }
    std::vector<std::string> now;
    {
        std::lock_guard<std::mutex> lk(mu_);
        now = skip_cats_;
    }
    auto list = [](const std::vector<std::string> &v) {
        std::string out = "[";
        for (size_t i = 0; i < v.size(); i++) out += (i ? "," : "") + json_quote(v[i]);
        return out + "]";
    };
    Reply r;
    r.result = "{\"categories\":" + list(now) + ",\"all\":" + list(all) + "}";
    return r;
}

}  // namespace bbm
