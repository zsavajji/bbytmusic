// The list of tracks and the current index. Plain data, no threads: the Player locks around it.
// The current index follows its track through add, remove and move.
#pragma once
#include <cstdint>
#include <random>
#include <vector>

#include "player/track.h"

namespace bbm {

enum class Repeat { Off, All, One };
enum class QueueSort { Title, Artist, Album, Duration };

class Queue {
public:
    // index is clamped; -1 when empty. In shuffle mode with an rng the chosen track goes first and the
    // rest is shuffled, the given order being the one the mode goes back to.
    void set(std::vector<Track> tracks, int index, std::mt19937 *rng = nullptr);
    void add(std::vector<Track> tracks, bool play_next);  // after the current track, or at the end
    bool remove(int i);   // removing the current one leaves the index on the track that followed it
    bool move(int from, int to);
    bool jump(int i);
    void clear_others();                                  // keeps the current track only (none: empties)
    void shuffle_after_current(std::mt19937 &rng);        // what follows the current track
    // The whole list, stable; the index follows. In shuffle mode the sorted order is kept and the mode
    // goes off: true then.
    bool sort(QueueSort by, bool desc);
    bool shuffle() const { return shuffle_; }
    // On: what follows the current track is shuffled, the order before is remembered. Off: back to it.
    void set_shuffle(bool on, std::mt19937 &rng);
    void restore_shuffle_flag(bool on) { shuffle_ = on; original_.clear(); }   // a restart: nothing to go back to

    const Track *current() const { return index_ >= 0 ? &tracks_[size_t(index_)] : nullptr; }
    int index() const { return index_; }
    int size() const { return int(tracks_.size()); }
    const std::vector<Track> &tracks() const { return tracks_; }
    void set_repeat(Repeat r) { repeat_ = r; }
    Repeat repeat() const { return repeat_; }
    // What a manual Next goes to: the track after, or under All the first after the last; -1 at the end.
    int next_index() const {
        if (index_ < 0) return -1;
        return index_ + 1 < size() ? index_ + 1 : repeat_ == Repeat::All ? 0 : -1;
    }
    // What follows when the current track ends by itself: under One the same track.
    int auto_next_index() const { return index_ >= 0 && repeat_ == Repeat::One ? index_ : next_index(); }
    int prev_index() const { return index_ > 0 ? index_ - 1 : -1; }
    int remaining() const { return index_ < 0 ? 0 : size() - 1 - index_; }   // tracks after the current

private:
    void permute(const std::vector<size_t> &order);   // order[k]: the old position of the new k-th; the index follows

    std::vector<Track> tracks_;
    std::vector<uint64_t> serials_;   // one per track, so an entry is itself through reordering and duplicate ids
    std::vector<uint64_t> original_;  // serials in the order shuffle goes back to; only while shuffle_
    uint64_t next_serial_ = 1;
    bool shuffle_ = false;
    int index_ = -1;
    Repeat repeat_ = Repeat::Off;
};

}  // namespace bbm
