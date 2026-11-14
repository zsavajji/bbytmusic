// The list of tracks and the current index. Plain data, no threads: the Player locks around it.
// The current index follows its track through add, remove and move.
#pragma once
#include <vector>

#include "player/track.h"

namespace bbm {

class Queue {
public:
    void set(std::vector<Track> tracks, int index);       // index is clamped; -1 when empty
    void add(std::vector<Track> tracks, bool play_next);  // after the current track, or at the end
    bool remove(int i);   // removing the current one leaves the index on the track that followed it
    bool move(int from, int to);
    bool jump(int i);

    const Track *current() const { return index_ >= 0 ? &tracks_[size_t(index_)] : nullptr; }
    int index() const { return index_; }
    int size() const { return int(tracks_.size()); }
    const std::vector<Track> &tracks() const { return tracks_; }
    int next_index() const { return index_ >= 0 && index_ + 1 < size() ? index_ + 1 : -1; }
    int prev_index() const { return index_ > 0 ? index_ - 1 : -1; }
    int remaining() const { return index_ < 0 ? 0 : size() - 1 - index_; }   // tracks after the current

private:
    std::vector<Track> tracks_;
    int index_ = -1;
};

}  // namespace bbm
