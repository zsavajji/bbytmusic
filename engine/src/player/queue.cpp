#include "player/queue.h"

#include <algorithm>

namespace bbm {

void Queue::set(std::vector<Track> tracks, int index) {
    tracks_ = std::move(tracks);
    index_ = tracks_.empty() ? -1 : std::clamp(index, 0, size() - 1);
}

void Queue::add(std::vector<Track> tracks, bool play_next) {
    if (tracks.empty()) return;
    const size_t at = play_next ? size_t(index_ + 1) : tracks_.size();   // index_ -1 -> front
    tracks_.insert(tracks_.begin() + std::ptrdiff_t(at), std::make_move_iterator(tracks.begin()),
                   std::make_move_iterator(tracks.end()));
    if (index_ < 0) index_ = 0;
}

bool Queue::remove(int i) {
    if (i < 0 || i >= size()) return false;
    tracks_.erase(tracks_.begin() + i);
    if (tracks_.empty()) index_ = -1;
    else if (i < index_) index_--;
    else if (i == index_) index_ = std::min(index_, size() - 1);
    return true;
}

bool Queue::move(int from, int to) {
    if (from < 0 || from >= size() || to < 0 || to >= size()) return false;
    if (from == to) return true;
    Track t = std::move(tracks_[size_t(from)]);
    tracks_.erase(tracks_.begin() + from);
    tracks_.insert(tracks_.begin() + to, std::move(t));
    if (index_ == from) index_ = to;
    else if (from < index_ && to >= index_) index_--;
    else if (from > index_ && to <= index_) index_++;
    return true;
}

bool Queue::jump(int i) {
    if (i < 0 || i >= size()) return false;
    index_ = i;
    return true;
}

}  // namespace bbm
