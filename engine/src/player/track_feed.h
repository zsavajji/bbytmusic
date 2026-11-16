// The feed thread: calls the SourceFactory and Source::read, and keeps up to 10 min of segments
// ahead of the play position in a bounded buffer of chunks: a song is read whole at once, so a
// network that drops mid-song (a car in a tunnel) does not stop it. When the playing track is
// fully read it prepares the next one (factory, open, first segments) so playback can go on
// without a gap; a next track that will not open is tried again while there is time. It shares the Player's mutex: every public method below expects the caller to hold it
// (the feed thread holds it too, except around blocking factory and read calls).
#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "player/track.h"
#include "stream/source.h"

namespace bbm {

using SourceFactory = std::function<std::unique_ptr<Source>(const Track &, std::string &err)>;

// What the player thread consumes, in order: Begin, Seg..., End per track; or Fail.
struct FeedChunk {
    enum Kind { Begin, Seg, End, Fail } kind = Seg;
    int serial = 0;                 // one per Begin/Fail, so stale chunks can be told apart
    Track track;                    // Begin, Fail
    std::vector<uint8_t> init;      // Begin
    int64_t duration_ms = 0;        // Begin
    int64_t target_ms = 0;          // Begin: where playback is to start
    int first_segment = 0;          // Begin: segment_at(target - pre-roll)
    Segment seg;                    // Seg
    bool began = false;             // Fail: a Begin was sent for this track before
    std::string err;                // Fail
    int64_t cost_ms = 0;            // what this chunk counts for in the buffer
};

class TrackFeed {
public:
    using Successor = std::function<std::optional<Track>()>;   // the track after the current one
    // ponytail: in RAM, about 1.2 MB a minute of Opus (12 MB at most); a podcast is read 10 min
    // at a time. Spill to the disk cache if that memory is ever wanted back.
    static constexpr int64_t kAheadMs = 600000;

    TrackFeed(std::mutex &m, std::condition_variable &player_cv, SourceFactory factory, Successor next);
    ~TrackFeed();   // takes the mutex itself; joins the thread

    void start(const Track &t, int64_t ms);   // drop everything, begin t at ms (reuses its Source)
    void flush(bool keep_sources);            // drop everything and go idle; cancels a read in flight
    void set_playing(int serial);             // the player has reached this Begin/Fail
    void sync_next(const Track *successor);   // the queue changed: drop a prepared next that no longer fits
    bool pop(FeedChunk &out);
    bool has_chunk() const { return !chunks_.empty(); }
    int64_t buffered_ms() const { return buffered_; }
    int reads() const { return reads_; }

private:
    struct Entry {
        int serial = 0;
        Track track;
        std::unique_ptr<Source> src;
        int next_seg = 0, nseg = 0;
        bool done = false, next = false, cancelled = false, drop = false;
    };
    struct Plan { Track track; int64_t ms = 0; };

    void run();
    void open_entry(std::unique_lock<std::mutex> &lk);
    void open_next(std::unique_lock<std::mutex> &lk, const Track &t);
    void add_entry(std::unique_ptr<Source> src, const std::string &err, const Track &t, int64_t target_ms, bool is_next);
    void read_one(std::unique_lock<std::mutex> &lk, Entry &e);
    void push(FeedChunk c);
    void purge();
    void cancel_reading();

    std::mutex &m_;
    std::condition_variable &player_cv_;
    std::condition_variable cv_;
    SourceFactory factory_;
    Successor successor_;

    std::deque<FeedChunk> chunks_;
    int64_t buffered_ = 0;
    std::deque<Entry> entries_;       // the Sources: only the feed thread adds, reads or erases (sync_next may pop an idle back)
    std::optional<Plan> plan_;
    uint64_t epoch_ = 0, next_gen_ = 0;
    int serial_ = 0, playing_serial_ = 0, reading_serial_ = 0, reads_ = 0;
    Source *reading_ = nullptr;
    bool read_cancelled_ = false, idle_ = true, keep_ = true, quit_ = false;
    std::string opening_id_;
    std::thread thread_;   // last: starts in the constructor
};

}  // namespace bbm
