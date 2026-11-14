// The player: a queue of tracks, a feed thread that fetches their segments, and a player thread
// that demuxes, decodes and writes to the sink, going from one track into the next with the sink
// still open. Commands may come from any thread and return at once; events are delivered from
// the player thread.
//
// Locks: one mutex (m_) guards the queue, the status, the requests and the feed's buffer; the
// player thread holds it except around decoding, sink calls and listener calls; the feed thread
// holds it except around SourceFactory and Source::read calls. Both wait on condition variables.
#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/sink.h"
#include "player/decode_chain.h"
#include "player/queue.h"
#include "player/track.h"
#include "player/track_feed.h"
#include "stream/source.h"

namespace bbm {

using SinkFactory = std::function<std::unique_ptr<Sink>()>;
enum class PlayState { Stopped, Loading, Playing, Paused };

struct PlayerStatus {
    PlayState state = PlayState::Stopped;
    int index = -1;
    std::string track_id;
    int64_t position_ms = 0, duration_ms = 0;
    std::string error;
};

struct PlayerEvent {
    enum Kind { State, Track, Position, Queue, QueueLow, Error } kind;
    PlayerStatus status;
};

class Player {
public:
    Player(SourceFactory sources, SinkFactory sinks);
    ~Player();   // stops and joins
    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    void set_listener(std::function<void(const PlayerEvent &)> fn);   // called from the player thread

    // Commands: any thread, return at once.
    void play(std::vector<Track> tracks, int index, int64_t position_ms = 0);   // replace the queue and start
    void pause();
    void resume();
    void toggle();
    void stop();
    void seek(int64_t ms);
    void next();
    void previous();   // restarts the track if more than 3 s in, else the one before
    void jump(int index);
    void enqueue(std::vector<Track> tracks, bool play_next);
    void remove(int index);
    void move(int from, int to);

    PlayerStatus status() const;
    std::vector<Track> queue() const;

    // Test-only: the feed's buffer, and how many Source::read calls it has made.
    int64_t buffered_ms_for_test() const;
    int reads_for_test() const;

private:
    struct Req {
        bool load = false, seek = false, stop = false;
        int64_t ms = 0;
    };

    // player thread
    void run();
    bool apply_requests(std::unique_lock<std::mutex> &lk);
    void step(std::unique_lock<std::mutex> &lk);
    void on_chunk(std::unique_lock<std::mutex> &lk, FeedChunk &c);
    void begin_track(std::unique_lock<std::mutex> &lk, const FeedChunk &c);
    void write_pcm(std::unique_lock<std::mutex> &lk);
    void do_load(std::unique_lock<std::mutex> &lk);
    void do_seek(std::unique_lock<std::mutex> &lk);
    void do_stop(std::unique_lock<std::mutex> &lk);
    void do_pause(std::unique_lock<std::mutex> &lk);
    void do_resume(std::unique_lock<std::mutex> &lk);
    void report_error(std::unique_lock<std::mutex> &lk, const std::string &msg);
    void emit(std::unique_lock<std::mutex> &lk, PlayerEvent::Kind k);
    void queue_low_check(std::unique_lock<std::mutex> &lk);
    bool ensure_sink(int channels, std::string &err);
    void set_state(std::unique_lock<std::mutex> &lk, PlayState s);

    // callers hold m_
    bool pending() const;
    bool interrupted() const { return req_.load || req_.seek || req_.stop || quit_; }
    PlayerStatus snapshot() const;
    void queue_changed();
    void load_index(int index, int64_t ms);
    std::optional<Track> successor() const;

    SinkFactory sink_factory_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    Queue queue_;
    PlayerStatus st_;
    Req req_;
    bool want_pause_ = false, queue_dirty_ = false, quit_ = false;
    std::function<void(const PlayerEvent &)> listener_;
    TrackFeed feed_;

    // player thread only
    DecodeChain chain_;
    std::unique_ptr<Sink> sink_;
    bool sink_open_ = false;
    int sink_ch_ = 0;
    std::vector<int16_t> pcm_;
    bool awaiting_begin_ = false, announce_ = false, first_write_ = false, loading_ = false;
    int failures_ = 0, cur_serial_ = 0, skip_serial_ = 0;
    int64_t last_sec_ = -1;

    std::thread thread_;   // last
};

}  // namespace bbm
