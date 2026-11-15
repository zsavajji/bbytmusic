// Engine: the player's events, out to the clients, the store and now-playing; the radio.
#include <cstdio>
#include <cstring>
#include <set>

#include "engine/engine.h"
#include "engine/item_json.h"
#include "innertube/next.h"
#include "util/log.h"

namespace bbm {

// Who holds now-playing, from the service's own status object ("" off BB10).
static std::string np_holder() {
    std::string out;
    if (FILE *f = std::fopen("/pps/services/multimedia/mediaplayer/status", "r")) {
        char line[256];
        while (std::fgets(line, sizeof line, f))
            if (std::strncmp(line, "active_name", 11) == 0 || std::strncmp(line, "active_pid", 10) == 0) out += line;
        std::fclose(f);
    }
    for (char &c : out)
        if (c == '\n') c = ' ';
    return out;
}

// The player thread: emit at once, keep the bookkeeping under mu_, post the slow work.
void Engine::on_event(const PlayerEvent &e) {
    if (closing_) return;
    const PlayerStatus &s = e.status;
    const std::string pos =
        "\"position_ms\":" + std::to_string(s.position_ms) + ",\"duration_ms\":" + std::to_string(s.duration_ms);
    switch (e.kind) {
    case PlayerEvent::State: {
        log_info(std::string("state ") + state_name(s.state) + ", index " + std::to_string(s.index) + ", " + s.track_id +
                 " at " + std::to_string(s.position_ms) + " ms" + (s.error.empty() ? "" : ": " + s.error));
        server_.emit("state", std::string("\"state\":\"") + state_name(s.state) + "\",\"index\":" +
                                  std::to_string(s.index) + ",\"track_id\":" + json_quote(s.track_id) + "," + pos +
                                  ",\"error\":" + json_quote(s.error));
        if (s.state == PlayState::Stopped) {
            std::lock_guard<std::mutex> lk(mu_);
            history_end();   // stop leaves the track
        }
        post([this, s] {
            np_update(s, false);
            save_state(s);
        });
        break;
    }
    case PlayerEvent::Track: {
        log_info("track " + std::to_string(s.index) + ": " + s.track_id);
        Item it;
        {
            std::lock_guard<std::mutex> lk(mu_);
            history_end();
            hist_id_ = s.track_id;
            hist_pos_ = s.position_ms;
            skip_new_track(s.track_id);
            Track t;
            t.id = s.track_id;
            if (items_.find(t.id) == items_.end()) {   // only a restored track lacks one; ask the queue
                const std::vector<Track> q = player_->queue();
                if (s.index >= 0 && size_t(s.index) < q.size() && q[size_t(s.index)].id == t.id) t = q[size_t(s.index)];
            }
            it = item_for(t);
        }
        server_.emit("track", "\"index\":" + std::to_string(s.index) + ",\"track\":" + item_json(it));
        server_.emit("position", pos);   // the UI shows 0 after a track event until a position comes
        post([this, s, it] {
            np_item_ = it;
            np_update(s, true);
            save_state(s);
        });
        break;
    }
    case PlayerEvent::Position: {
        server_.emit("position", pos);
        bool save = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            const int64_t d = s.position_ms - hist_pos_;
            if (s.track_id == hist_id_ && d > 0 && d <= 2500) hist_ms_ += d;   // a seek is not listening
            hist_pos_ = s.position_ms;
            const auto now = std::chrono::steady_clock::now();
            if (now - last_save_ >= std::chrono::seconds(5)) {
                last_save_ = now;
                save = true;
            }
        }
        if (save) post([this, s] { save_state(s); });
        skip_check(s);
        break;
    }
    case PlayerEvent::Queue:
        server_.emit("queue", "\"len\":" + std::to_string(player_->queue().size()) + ",\"index\":" + std::to_string(s.index));
        post([this] { save_queue(); });
        break;
    case PlayerEvent::QueueLow:
        radio_more();
        break;
    case PlayerEvent::Error:
        server_.emit("error", "\"message\":" + json_quote(s.error) + ",\"track_id\":" + json_quote(s.track_id));
        break;
    }
}

// The worker: the system's now-playing, started on the first play (not before: acquiring it
// would take the media keys from another player as soon as the UI opens), and taken back after a
// revoke only when we play again.
void Engine::np_update(const PlayerStatus &s, bool send_meta) {
    const bool playing = s.state == PlayState::Playing || s.state == PlayState::Loading;
    std::string err;
    if (!np_started_) {
        if (!playing || np_failed_) return;
        if (!np_.start("bbmusic", [this](const std::string &c) { on_np_command(c); }, err)) {
            np_failed_ = true;
            log_info("now-playing: " + err);
            return;
        }
        np_started_ = true;
        send_meta = true;
    } else if (np_revoked_) {
        if (!playing) return;   // another player has the overlay
        if (!np_.acquire(err)) {
            log_warn("now-playing: " + err);
            return;
        }
        np_revoked_ = false;
        send_meta = true;
    }
    if (send_meta) {
        const bool has_next = s.index >= 0 && size_t(s.index) + 1 < player_->queue().size();
        // The cover is the file Now Playing shows; if it is not cached yet the fetch is queued and
        // np_art() sends the metadata again when it lands.
        const auto have = thumbs_->request({np_item_.thumb}, kArtSize);
        const std::string art = have.empty() ? "" : have.begin()->second;
        if (!art.empty()) log_info("now-playing: art " + art);
        // ponytail: the overlay is another process (/usr/sbin/nowplaying) and may not read a file
        // in our sandbox (<data_dir>/thumbs); if the phone shows no art, copy it to a world-readable
        // place (the shared folder) and pass that.
        np_.set_metadata(np_item_.title, np_item_.artist, np_item_.album, s.duration_ms, s.position_ms, has_next, true, art);
    }
    np_.set_state(playing ? NowPlaying::State::Playing
                          : s.state == PlayState::Paused ? NowPlaying::State::Paused : NowPlaying::State::Stopped);
}

// The worker, when a cover at kArtSize has been fetched: if it is the playing song's, send the
// metadata again with it.
void Engine::np_art(const std::string &url) {
    if (closing_ || !np_started_ || np_revoked_ || url != np_item_.thumb) return;
    np_update(player_->status(), true);
}

// The overlay's buttons, on NowPlaying's reader thread.
void Engine::on_np_command(const std::string &c) {
    if (closing_) return;
    log_info("now-playing: " + c);
    if (c == "play") resume(false);
    else if (c == "pause") player_->pause();
    else if (c == "next") player_->next();
    else if (c == "prev") player_->previous();
    else if (c == "revoked") {
        log_info("now-playing: taken by " + np_holder());
        np_revoked_ = true;
        player_->pause();
    }
}

// The player thread, on QueueLow: ask for the radio's next page, once per page.
void Engine::radio_more() {
    std::string cont;
    uint64_t gen;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (radio_next_.empty() || radio_next_ == radio_asked_) return;
        radio_asked_ = cont = radio_next_;
        gen = radio_gen_;
    }
    post([this, cont, gen] {
        QueuePage page;
        std::string err;
        const bool ok = yt_call([&](InnerTube &yt, std::string &e) { return watch_queue_more(yt, cont, page, e); }, err);
        std::lock_guard<std::mutex> lk(mu_);
        if (gen != radio_gen_) return;   // another play since
        if (!ok) {
            log_warn("radio: " + err);
            radio_asked_.clear();   // the next QueueLow tries again
            return;
        }
        radio_next_ = page.continuation;
        radio_append(page.items);
    });
}

// The first page of a song's radio, on the caller's thread; appended unless another play or radio
// has come since `gen`. A failure is only logged: the song plays on without it.
void Engine::radio_first(const std::string &id, uint64_t gen) {
    QueuePage page;
    std::string err;
    if (!yt_call([&](InnerTube &yt, std::string &e) { return watch_queue(yt, id, "", page, e); }, err)) {
        log_warn("radio of " + id + ": " + err);
        return;
    }
    std::lock_guard<std::mutex> lk(mu_);
    if (gen == radio_gen_) {
        radio_next_ = page.continuation;
        radio_append(page.items);
    }
}

// Appends what can be played and is not queued yet (the first page starts with the song itself).
void Engine::radio_append(const std::vector<Item> &items) {
    std::set<std::string> have;
    for (const Track &t : player_->queue()) have.insert(t.id);
    std::vector<Track> add;
    for (const Item &i : items) {
        if (!playable(i) || !have.insert(i.id).second) continue;
        items_[i.id] = i;
        add.push_back(to_track(i));
    }
    log_info("radio: " + std::to_string(add.size()) + " more" + (radio_next_.empty() ? ", the last page" : ""));
    if (!add.empty()) player_->enqueue(std::move(add), false);
}

}  // namespace bbm
