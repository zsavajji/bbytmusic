#include "player/queue.h"

#include <algorithm>
#include <cctype>
#include <numeric>

namespace bbm {

void Queue::set(std::vector<Track> tracks, int index, std::mt19937 *rng) {
    tracks_ = std::move(tracks);
    serials_.resize(tracks_.size());
    for (uint64_t &s : serials_) s = next_serial_++;
    index_ = tracks_.empty() ? -1 : std::clamp(index, 0, size() - 1);
    original_.clear();
    if (!shuffle_ || !rng || index_ < 0) return;
    original_ = serials_;
    // the chosen track first, the rest shuffled
    std::swap(tracks_[0], tracks_[size_t(index_)]);
    std::swap(serials_[0], serials_[size_t(index_)]);
    index_ = 0;
    shuffle_after_current(*rng);
}

void Queue::add(std::vector<Track> tracks, bool play_next) {
    if (tracks.empty()) return;
    const bool track_original = shuffle_ && original_.size() == serials_.size();   // not after a restart
    const size_t at = play_next ? size_t(index_ + 1) : tracks_.size();   // index_ -1 -> front
    const size_t n = tracks.size();
    tracks_.insert(tracks_.begin() + std::ptrdiff_t(at), std::make_move_iterator(tracks.begin()),
                   std::make_move_iterator(tracks.end()));
    std::vector<uint64_t> added(n);
    for (uint64_t &s : added) s = next_serial_++;
    if (track_original) {
        size_t o = original_.size();
        if (play_next) {   // right after the current entry's place in the original order
            o = 0;
            if (index_ >= 0)
                o = size_t(std::find(original_.begin(), original_.end(), serials_[size_t(index_)]) - original_.begin()) + 1;
            o = std::min(o, original_.size());
        }
        original_.insert(original_.begin() + std::ptrdiff_t(o), added.begin(), added.end());
    }
    serials_.insert(serials_.begin() + std::ptrdiff_t(at), added.begin(), added.end());
    if (index_ < 0) index_ = 0;
}

bool Queue::remove(int i) {
    if (i < 0 || i >= size()) return false;
    if (auto o = std::find(original_.begin(), original_.end(), serials_[size_t(i)]); o != original_.end()) original_.erase(o);
    tracks_.erase(tracks_.begin() + i);
    serials_.erase(serials_.begin() + i);
    if (tracks_.empty()) index_ = -1;
    else if (i < index_) index_--;
    else if (i == index_) index_ = std::min(index_, size() - 1);
    return true;
}

bool Queue::move(int from, int to) {
    if (from < 0 || from >= size() || to < 0 || to >= size()) return false;
    if (from == to) return true;
    Track t = std::move(tracks_[size_t(from)]);
    const uint64_t s = serials_[size_t(from)];
    tracks_.erase(tracks_.begin() + from);
    serials_.erase(serials_.begin() + from);
    tracks_.insert(tracks_.begin() + to, std::move(t));
    serials_.insert(serials_.begin() + to, s);
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
    original_.clear();
    if (index_ < 0) {
        tracks_.clear();
        serials_.clear();
        return;
    }
    Track cur = std::move(tracks_[size_t(index_)]);
    const uint64_t s = serials_[size_t(index_)];
    tracks_.clear();
    tracks_.push_back(std::move(cur));
    serials_.assign(1, s);
    if (shuffle_) original_ = serials_;
    index_ = 0;
}

void Queue::permute(const std::vector<size_t> &order) {
    std::vector<Track> t;
    std::vector<uint64_t> s;
    t.reserve(order.size());
    s.reserve(order.size());
    int now = index_;
    for (size_t k = 0; k < order.size(); k++) {
        if (int(order[k]) == index_) now = int(k);
        t.push_back(std::move(tracks_[order[k]]));
        s.push_back(serials_[order[k]]);
    }
    tracks_ = std::move(t);
    serials_ = std::move(s);
    index_ = now;
}

void Queue::shuffle_after_current(std::mt19937 &rng) {
    std::vector<size_t> order(tracks_.size());   // shuffle positions, then apply them to tracks and serials alike
    std::iota(order.begin(), order.end(), size_t(0));
    std::shuffle(order.begin() + (index_ + 1), order.end(), rng);   // index_ -1: everything
    permute(order);
}

void Queue::set_shuffle(bool on, std::mt19937 &rng) {
    if (on == shuffle_) return;
    shuffle_ = on;
    if (on) {
        original_ = serials_;
        shuffle_after_current(rng);
        return;
    }
    if (original_.empty()) return;   // after a restart: nothing to go back to
    constexpr size_t kLast = size_t(-1);
    std::vector<size_t> rank(serials_.size(), kLast), order(serials_.size());
    for (size_t k = 0; k < serials_.size(); k++)
        if (auto o = std::find(original_.begin(), original_.end(), serials_[k]); o != original_.end())
            rank[k] = size_t(o - original_.begin());
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return rank[a] < rank[b]; });
    permute(order);
    original_.clear();
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

bool Queue::sort(QueueSort by, bool desc) {
    std::vector<size_t> order(tracks_.size());   // positions, so a duplicate id cannot confuse the current one
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin() + (index_ + 1), order.end(), [&](size_t a, size_t b) {   // index_ -1: everything
        const int c = compare(tracks_[a], tracks_[b], by);
        return desc ? c > 0 : c < 0;
    });
    permute(order);
    const bool was = shuffle_;   // the sorted order is the order now
    shuffle_ = false;
    original_.clear();
    return was;
}

}  // namespace bbm
