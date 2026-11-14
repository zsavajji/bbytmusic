#include "player/queue.h"

#include <algorithm>
#include <cctype>
#include <numeric>

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

void Queue::clear_others() {
    if (index_ < 0) {
        tracks_.clear();
        return;
    }
    Track cur = std::move(tracks_[size_t(index_)]);
    tracks_.clear();
    tracks_.push_back(std::move(cur));
    index_ = 0;
}

void Queue::shuffle_after_current(std::mt19937 &rng) {
    std::shuffle(tracks_.begin() + (index_ + 1), tracks_.end(), rng);   // index_ -1: everything
}

namespace {
// ponytail: ASCII-only case folding; accented and non-Latin titles sort by their bytes. ICU or
// a locale collation if that ever matters.
int compare_text(const std::string &a, const std::string &b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++) {
        const int x = std::tolower(static_cast<unsigned char>(a[i])), y = std::tolower(static_cast<unsigned char>(b[i]));
        if (x != y) return x < y ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : a.size() < b.size() ? -1 : 1;
}

int compare(const Track &a, const Track &b, QueueSort by) {
    switch (by) {
    case QueueSort::Title: return compare_text(a.title, b.title);
    case QueueSort::Artist: return compare_text(a.artist, b.artist);
    case QueueSort::Album: return compare_text(a.album, b.album);
    case QueueSort::Duration: return a.duration_s < b.duration_s ? -1 : a.duration_s > b.duration_s ? 1 : 0;
    }
    return 0;
}
}  // namespace

void Queue::sort(QueueSort by, bool desc) {
    std::vector<size_t> order(tracks_.size());   // positions, so a duplicate id cannot confuse the current one
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const int c = compare(tracks_[a], tracks_[b], by);
        return desc ? c > 0 : c < 0;
    });
    std::vector<Track> sorted;
    sorted.reserve(order.size());
    int now = index_;
    for (size_t k = 0; k < order.size(); k++) {
        if (int(order[k]) == index_) now = int(k);
        sorted.push_back(std::move(tracks_[order[k]]));
    }
    tracks_ = std::move(sorted);
    index_ = now;
}

}  // namespace bbm
