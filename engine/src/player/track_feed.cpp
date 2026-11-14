#include "player/track_feed.h"

#include <algorithm>

namespace bbm {
namespace {
constexpr int64_t kPrerollMs = 80;   // the decoder's seek pre-roll (WebM SeekPreRoll), as ffmpeg and YouTube write it
}

TrackFeed::TrackFeed(std::mutex &m, std::condition_variable &player_cv, SourceFactory factory, Successor next)
    : m_(m), player_cv_(player_cv), factory_(std::move(factory)), successor_(std::move(next)),
      thread_([this] { run(); }) {}

TrackFeed::~TrackFeed() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
        cancel_reading();
    }
    cv_.notify_all();
    thread_.join();
}

void TrackFeed::purge() {
    chunks_.clear();
    buffered_ = 0;
}

void TrackFeed::cancel_reading() {
    if (reading_) {
        reading_->cancel();
        read_cancelled_ = true;
    }
}

void TrackFeed::start(const Track &t, int64_t ms) {
    ++epoch_;
    plan_ = Plan{t, ms};
    idle_ = false;
    keep_ = true;
    purge();
    cancel_reading();
    cv_.notify_all();
}

void TrackFeed::flush(bool keep_sources) {
    ++epoch_;
    plan_.reset();
    idle_ = true;
    keep_ = keep_sources;
    purge();
    cancel_reading();
    cv_.notify_all();
}

void TrackFeed::set_playing(int serial) {
    playing_serial_ = serial;
    cv_.notify_all();
}

void TrackFeed::sync_next(const Track *succ) {
    if (!entries_.empty()) {
        Entry &b = entries_.back();
        if (b.next && b.serial != playing_serial_ && (!succ || succ->id != b.track.id)) {
            std::erase_if(chunks_, [&](const FeedChunk &c) {
                if (c.serial != b.serial) return false;
                buffered_ -= c.cost_ms;
                return true;
            });
            if (reading_ && reading_serial_ == b.serial) {
                cancel_reading();
                b.drop = true;   // the feed thread erases it when the read returns
            } else {
                entries_.pop_back();
            }
        }
    }
    if (!opening_id_.empty() && (!succ || succ->id != opening_id_)) ++next_gen_;   // a stale open is discarded
    cv_.notify_all();
}

bool TrackFeed::pop(FeedChunk &out) {
    if (chunks_.empty()) return false;
    out = std::move(chunks_.front());
    chunks_.pop_front();
    buffered_ -= out.cost_ms;
    cv_.notify_all();
    return true;
}

void TrackFeed::push(FeedChunk c) {
    chunks_.push_back(std::move(c));
    player_cv_.notify_all();
}

// An opened Source becomes an entry plus its Begin chunk; a failure becomes a Fail chunk.
void TrackFeed::add_entry(std::unique_ptr<Source> src, const std::string &err, const Track &t,
                          int64_t target_ms, bool is_next) {
    Entry e;
    e.serial = ++serial_;
    e.track = t;
    e.next = is_next;
    FeedChunk c;
    c.serial = e.serial;
    c.track = t;
    std::string why = err;
    if (src && src->segment_count() <= 0) why = "the stream has no segments";
    if (!src || !why.empty()) {
        c.kind = FeedChunk::Fail;
        c.err = why.empty() ? "could not open the stream" : why;
        e.done = true;
        entries_.push_back(std::move(e));
        push(std::move(c));
        return;
    }
    e.nseg = src->segment_count();
    if (target_ms > 0) e.next_seg = std::clamp(src->segment_at(std::max<int64_t>(0, target_ms - kPrerollMs)), 0, e.nseg - 1);
    c.kind = FeedChunk::Begin;
    c.init = src->init();
    c.duration_ms = src->duration_ms() > 0 ? src->duration_ms() : int64_t(t.duration_s) * 1000;
    c.target_ms = target_ms;
    c.first_segment = e.next_seg;
    e.src = std::move(src);
    entries_.push_back(std::move(e));
    push(std::move(c));
}

void TrackFeed::open_entry(std::unique_lock<std::mutex> &lk) {
    const Plan p = *plan_;
    plan_.reset();
    const uint64_t ep = epoch_;
    std::unique_ptr<Source> src;
    for (Entry &e : entries_)   // a seek in the same track keeps its Source
        if (e.src && !e.cancelled && e.track.id == p.track.id) { src = std::move(e.src); break; }
    entries_.clear();
    std::string err;
    if (!src) {
        lk.unlock();
        src = factory_(p.track, err);
        lk.lock();
        if (ep != epoch_ || quit_) return;   // overtaken: the loop picks up the new plan
    }
    add_entry(std::move(src), err, p.track, p.ms, false);
}

void TrackFeed::open_next(std::unique_lock<std::mutex> &lk, const Track &t) {
    const uint64_t ep = epoch_, gen = next_gen_;
    opening_id_ = t.id;
    lk.unlock();
    std::string err;
    std::unique_ptr<Source> src = factory_(t, err);
    lk.lock();
    opening_id_.clear();
    if (ep != epoch_ || gen != next_gen_ || quit_) return;
    add_entry(std::move(src), err, t, 0, true);
}

void TrackFeed::read_one(std::unique_lock<std::mutex> &lk, Entry &e) {
    const int idx = e.next_seg;
    const uint64_t ep = epoch_;
    reading_ = e.src.get();
    reading_serial_ = e.serial;
    read_cancelled_ = false;
    ++reads_;
    lk.unlock();
    Segment seg;
    std::string err;
    const bool ok = e.src->read(idx, seg, err);
    lk.lock();
    reading_ = nullptr;
    reading_serial_ = 0;
    if (read_cancelled_) { e.cancelled = true; read_cancelled_ = false; }   // cancel() may be sticky: do not reuse it
    if (e.drop) { entries_.pop_back(); return; }
    if (ep != epoch_ || quit_) return;
    FeedChunk c;
    c.serial = e.serial;
    if (!ok) {
        c.kind = FeedChunk::Fail;
        c.track = e.track;
        c.began = true;
        c.err = err.empty() ? "read failed" : err;
        e.done = true;
        push(std::move(c));
        return;
    }
    c.kind = FeedChunk::Seg;
    c.cost_ms = seg.duration_ms > 0 ? seg.duration_ms : 1000;
    buffered_ += c.cost_ms;
    c.seg = std::move(seg);
    push(std::move(c));
    if (++e.next_seg >= e.nseg) {
        e.done = true;
        FeedChunk end;
        end.kind = FeedChunk::End;
        end.serial = e.serial;
        push(std::move(end));
    }
}

void TrackFeed::run() {
    std::unique_lock<std::mutex> lk(m_);
    uint64_t seen = epoch_;
    while (!quit_) {
        if (seen != epoch_) {
            seen = epoch_;
            std::erase_if(entries_, [](const Entry &e) { return e.cancelled || e.drop; });
            if (!keep_) entries_.clear();
        }
        while (entries_.size() > 1 && entries_.front().serial < playing_serial_) entries_.pop_front();
        if (idle_) { cv_.wait(lk); continue; }
        if (plan_) { open_entry(lk); continue; }
        if (entries_.empty()) { cv_.wait(lk); continue; }
        Entry &b = entries_.back();
        if (!b.done) {
            if (buffered_ < kAheadMs) read_one(lk, b);
            else cv_.wait(lk);
            continue;
        }
        if (b.serial == playing_serial_)
            if (std::optional<Track> s = successor_()) { open_next(lk, *s); continue; }
        cv_.wait(lk);
    }
}

}  // namespace bbm
