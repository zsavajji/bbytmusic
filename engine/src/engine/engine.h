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
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iterator>
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
#include "engine/account.h"
#include "engine/np_art.h"
#include "engine/sponsor.h"
#include "engine/thumbs.h"
#include "net/http.h"
#include "player/player.h"
#include "server/server.h"
#include "store/db.h"
#include "stream/segment_cache.h"

namespace bbm {

// The sizes of the song cache the user can pick, in MB.
inline constexpr int kCacheMbChoices[] = {128, 256, 512, 1024, 2048};
inline bool valid_cache_mb(int64_t mb) {
    return std::find(std::begin(kCacheMbChoices), std::end(kCacheMbChoices), mb) != std::end(kCacheMbChoices);
}

struct EngineOptions {
    std::string data_dir;                   // engine.json, music.db, cache/
    std::string version = "0";
    SinkFactory sinks;                      // required
    SourceFactory sources;                  // empty: resolve() + make_source() behind the cache
    int64_t cache_bytes = int64_t(256) << 20;
    int64_t idle_exit_ms = 10 * 60 * 1000;  // nothing playing and no client this long: exit
    // Where the now-playing overlay's cover is copied (it cannot read data_dir): a dot-folder in the
    // shared storage, which its group (1000_shared) reads and the Pictures app does not list. Needs
    // the access_shared permission; where it is not there (the host) no cover is sent.
    std::string np_art_dir = "/accounts/1000/shared/misc/.bbmusic";
    // SponsorBlock's segments of a video, on the worker thread; empty: fetch_segments() on the engine's Http.
    std::function<bool(const std::string &video_id, const std::vector<std::string> &categories,
                       std::vector<SkipSegment> &out, std::string &err)> segments;
    // The account's requests (the TV page, the token, the account's name); empty: an Http of its own.
    HttpSendFn account_http;
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
    Reply radio_command();
    Reply queue_add(Json req);
    Reply resume(bool toggle);
    std::string mode_json();   // the payload of a mode event: repeat and eq
    // Runs one InnerTube call under it_mu_, then hands its visitor data to resolve() once.
    // First, on the first start, the country is asked for (once per process).
    bool yt_call(const std::function<bool(InnerTube &, std::string &)> &call, std::string &err);
    void set_region(const std::string &gl);   // it_mu_ held: use it and keep it
    void detect_region();                     // it_mu_ held
    // events.cpp
    void on_event(const PlayerEvent &e);
    void on_np_command(const std::string &c);
    void np_update(const PlayerStatus &s, bool send_meta);       // worker
    void np_art(const std::string &url);                         // worker
    void history_end();                                          // mu_ held
    void radio_first(const std::string &id, uint64_t gen);       // not under mu_
    void radio_more();
    void radio_append(const std::vector<Item> &items);           // mu_ held
    // skip.cpp
    Reply skip_command(Json req);
    void skip_new_track(const std::string &id);                  // mu_ held
    void skip_fetch(const std::string &id, std::vector<std::string> cats);
    void skip_check(const PlayerStatus &s);                      // not under mu_
    // account_cmd.cpp
    Reply account_command(Json req);
    std::string account_json();   // the fields of the account event and of the command's result, no braces
    // favorites.cpp
    Reply favorites_command();
    Reply favorite_command(Json req);
    void fav_restore();                          // start-up: the stored favourites are kept and queued
    void fav_enqueue(const std::string &id);
    void fav_requeue();                          // every favourite that is not offline
    void fav_download();                         // the downloader thread
    void fav_fetch(const std::string &id);       // one track into the cache; marks it offline when whole
    void fav_event(const std::string &id, bool on, bool offline);
    bool on_store(std::function<void()> job);    // runs job on the worker and waits; false: no store, or too slow
    // library.cpp
    bool tv_call(const std::string &host, const std::string &endpoint, const std::string &fields, JsonDoc &out, Reply &fail);
    Reply tv_browse(const std::string &id);          // a library tab, or a playlist, as a browse result
    Reply tv_browse_more(const std::string &token);  // `token` still carries its "tvf:" or "tvp:" prefix
    Reply like_command(Json req);
    // engine.cpp
    void post(std::function<void()> job);
    void work();
    bool idle_check();   // worker: true when it is time to exit
    std::unique_ptr<Source> open_source(const Track &t, std::string &err);
    void restore();
    void save_queue();                                           // worker
    void save_state(const PlayerStatus &s);                      // worker
    Item item_for(const Track &t);                               // mu_ held

    static constexpr int kArtSize = 544;   // the cover now-playing asks for: the file Now Playing shows

    EngineOptions opt_;
    Server server_;
    Db db_;
    bool db_ok_ = false;
    SegmentCache cache_;

    std::mutex it_mu_;   // the InnerTube: one request at a time
    InnerTube yt_;
    bool visitor_shared_ = false;
    bool region_known_ = false;   // stored, chosen, or detection was tried: it is not tried again

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
    std::set<std::string> fav_ids_;       // the favourites; fav_done_ of them are whole in the cache
    std::set<std::string> fav_done_;
    int64_t skip_last_to_ = -1;           // the segment skipped last, so as not to skip it twice
    std::chrono::steady_clock::time_point skip_last_at_;

    // worker only
    std::set<std::string> saved_tracks_;
    Http sponsor_http_;   // the segments' fetches
    NowPlaying np_;
    bool np_started_ = false, np_failed_ = false;
    Item np_item_;
    NpArt np_art_{opt_.np_art_dir};
    // What the overlay was last sent of the track: sent again when the Next button, the duration or the cover differ.
    bool np_sent_next_ = false;
    int64_t np_sent_ms_ = 0;
    std::string np_sent_art_, np_sent_line_;   // the cover; the artist line ("Title – Artist" off Bluetooth)
    std::atomic<bool> np_revoked_{false};
    std::chrono::steady_clock::time_point idle_since_;
    bool idle_ = false;

    std::mutex wq_mu_;
    std::condition_variable wq_cv_;
    std::deque<std::function<void()>> jobs_;
    bool worker_quit_ = false;
    std::thread worker_;
    std::atomic<bool> closing_{false};

    // The favourites' downloader: one track at a time into the cache.
    std::thread fav_thread_;
    std::mutex fav_mu_;
    std::condition_variable fav_cv_;
    std::deque<std::string> fav_queue_;
    bool fav_quit_ = false;
    Source *fav_src_ = nullptr;   // the source being read, so that the destructor can cancel it

    std::unique_ptr<Account> account_;   // its on_change emits and posts: guarded by closing_
    Equalizer eq_;                     // the player's thread uses it through the sink
    std::unique_ptr<Thumbs> thumbs_;   // its ready() emits through server_ and posts to the worker
    std::unique_ptr<Player> player_;   // last: its threads call into everything above
};

// library.cpp: the ids the account answers (its library tabs and Liked Music).
bool library_id(const std::string &id);

}  // namespace bbm
