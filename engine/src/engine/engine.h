// bbmusicd's wiring: the server's commands drive the player and InnerTube; the player's events go
// out to every client, to the store and to the system's now-playing.
//
// Threads. Server::run() is the caller's thread (run()). Each connection's reader thread runs its
// commands, network work included: the one InnerTube sits behind it_mu_. The player has its own
// threads and calls on_event() from its player thread, which only emits, keeps a little
// bookkeeping under mu_ and posts the rest to the worker. The worker thread does the store's
// writes, the radio's continuations, the now-playing calls, the idle exit and the quit.
// Locks are taken in the order mu_, then the Player's or the Server's own; never the other way.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "audio/equalizer.h"
#include "innertube/client.h"
#include "innertube/items.h"
#include "platform/now_playing.h"
#include "engine/sponsor.h"
#include "engine/thumbs.h"
#include "net/http.h"
#include "player/player.h"
#include "server/server.h"
#include "store/db.h"
#include "stream/segment_cache.h"

namespace bbm {

struct EngineOptions {
    std::string data_dir;                   // engine.json, music.db, cache/
    std::string version = "0";
    SinkFactory sinks;                      // required
    SourceFactory sources;                  // empty: resolve() + make_source() behind the cache
    int64_t cache_bytes = int64_t(256) << 20;
    int64_t idle_exit_ms = 10 * 60 * 1000;  // nothing playing and no client this long: exit
    // SponsorBlock's segments of a video, on the worker thread; empty: fetch_segments() on the engine's Http.
    std::function<bool(const std::string &video_id, const std::vector<std::string> &categories,
                       std::vector<SkipSegment> &out, std::string &err)> segments;
};

class Engine {
public:
    explicit Engine(EngineOptions o);
    ~Engine();
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    // Opens the store, restores the queue (paused), listens and writes engine.json.
    bool start(std::string &err);
    void run();    // blocks until quit, stop() or the idle exit; saves the state on the way out
    void stop();   // any thread

    int port() const { return server_.port(); }
    const std::string &token() const { return server_.token(); }
    int media_fetches() const { return cache_.fetches; }   // what the cache could not serve

private:
    // commands.cpp
    void add_commands();
    Reply status();
    Reply play(Json req);
    Reply queue_add(Json req);
    Reply resume(bool toggle);
    std::string mode_json();   // the payload of a mode event: repeat and eq
    // Runs one InnerTube call under it_mu_, then hands its visitor data to resolve() once.
    bool yt_call(const std::function<bool(InnerTube &, std::string &)> &call, std::string &err);
    // events.cpp
    void on_event(const PlayerEvent &e);
    void on_np_command(const std::string &c);
    void np_update(const PlayerStatus &s, bool send_meta);       // worker
    void history_end();                                          // mu_ held
    void radio_more();
    void radio_append(const std::vector<Item> &items);           // mu_ held
    // skip.cpp
    Reply skip_command(Json req);
    void skip_new_track(const std::string &id);                  // mu_ held
    void skip_fetch(const std::string &id, std::vector<std::string> cats);
    void skip_check(const PlayerStatus &s);                      // not under mu_
    // engine.cpp
    void post(std::function<void()> job);
    void work();
    bool idle_check();   // worker: true when it is time to exit
    std::unique_ptr<Source> open_source(const Track &t, std::string &err);
    void restore();
    void save_queue();                                           // worker
    void save_state(const PlayerStatus &s);                      // worker
    Item item_for(const Track &t);                               // mu_ held

    EngineOptions opt_;
    Server server_;
    Db db_;
    bool db_ok_ = false;
    SegmentCache cache_;

    std::mutex it_mu_;   // the InnerTube: one request at a time
    InnerTube yt_;
    bool visitor_shared_ = false;

    std::mutex mu_;      // what follows, up to the worker's queue
    std::map<std::string, Item> items_;   // everything known of the queued tracks, by id
    uint64_t radio_gen_ = 0;              // one per play: an older radio answer is thrown away
    std::string radio_next_, radio_asked_;   // the next page's continuation; the one asked for
    std::string hist_id_;                 // the track being listened to, and for how long
    int64_t hist_ms_ = 0, hist_pos_ = 0;
    std::chrono::steady_clock::time_point last_save_;
    std::vector<std::string> skip_cats_;  // the categories skipped; empty: SponsorBlock is off
    std::string skip_track_;              // the current track, and its segments
    std::vector<SkipSegment> skip_segs_;
    int64_t skip_last_to_ = -1;           // the segment skipped last, so as not to skip it twice
    std::chrono::steady_clock::time_point skip_last_at_;

    // worker only
    std::set<std::string> saved_tracks_;
    Http sponsor_http_;   // the segments' fetches
    NowPlaying np_;
    bool np_started_ = false, np_failed_ = false;
    Item np_item_;
    std::atomic<bool> np_revoked_{false};
    std::chrono::steady_clock::time_point idle_since_;
    bool idle_ = false;

    std::mutex wq_mu_;
    std::condition_variable wq_cv_;
    std::deque<std::function<void()>> jobs_;
    bool worker_quit_ = false;
    std::thread worker_;
    std::atomic<bool> closing_{false};

    Equalizer eq_;                     // the player's thread uses it through the sink
    std::unique_ptr<Thumbs> thumbs_;   // its ready() emits through server_
    std::unique_ptr<Player> player_;   // last: its threads call into everything above
};

}  // namespace bbm
