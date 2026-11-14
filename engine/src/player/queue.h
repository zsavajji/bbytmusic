// The list of tracks and the current index. Plain data, no threads: the Player locks around it.
// The current index follows its track through add, remove and move.
#pragma once
#include <random>
#include <vector>

#include "player/track.h"

namespace bbm {

enum class Repeat { Off, All, One };
enum class QueueSort { Title, Artist, Album, Duration };

class Queue {
public:
    void set(std::vector<Track> tracks, int index);       // index is clamped; -1 when empty
    void add(std::vector<Track> tracks, bool play_next);  // after the current track, or at the end
    bool remove(int i);   // removing the current one leaves the index on the track that followed it
    bool move(int from, int to);
    bool jump(int i);
    void clear_others();                                  // keeps the current track only (none: empties)
    void shuffle_after_current(std::mt19937 &rng);        // what follows the current track
    void sort(QueueSort by, bool desc);                   // the whole list, stable; the index follows

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
    std::vector<Track> tracks_;
    int index_ = -1;
    Repeat repeat_ = Repeat::Off;
};

}  // namespace bbm
