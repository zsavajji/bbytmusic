// Test doubles for the player tests (header only: each test program is one source file):
// FileSource, a Source over a local WebM file; CaptureSink, a Sink that keeps what it is given;
// and a recorder for the player's events.
#pragma once
#include <atomic>
#include <chrono>
#include <cstring>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/sink.h"
#include "player/player.h"
#include "stream/source.h"
#include "util/ebml.h"

namespace ptest {
using namespace bbm;

inline std::string fixture(const char *name) { return std::string(BBM_FIXTURES) + "/player/" + name; }

// init() is everything before the first Cluster; each Cluster is a segment, its start from the
// Cluster's Timecode and its duration up to the next one (the last repeats the one before).
class FileSource : public Source {
public:
    struct Options {
        int delay_ms = 0;        // each read() takes this long (cancel() cuts it short)
        int fail_at = -1;        // read() of this segment fails
        bool fail_open = false;
        int loops = 1;           // the clusters repeated this many times (a longer track)
    };
    FileSource(const std::string &path, Options o) : opt_(o) {
        std::ifstream f(path, std::ios::binary);
        file_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    explicit FileSource(const std::string &path) : FileSource(path, Options{}) {}

    bool open(std::string &err) override {
        if (opt_.fail_open) { err = "open failed (test)"; return false; }
        if (!index()) { err = "bad file"; return false; }
        return true;
    }
    const std::vector<uint8_t> &init() const override { return init_; }
    int segment_count() const override { return int(segs_.size()) * opt_.loops; }
    int64_t duration_ms() const override { return total_ * opt_.loops; }
    int segment_at(int64_t ms) const override {
        const int n = int(segs_.size());
        const int64_t lap = ms / total_;
        const int64_t r = ms % total_;
        int i = 0;
        for (int k = 0; k < n; k++) if (segs_[size_t(k)].start <= r) i = k;
        const int64_t idx = lap * n + i;
        return int(std::min<int64_t>(idx, segment_count() - 1));
    }
    bool read(int index, Segment &out, std::string &err) override {
        ++reads;
        if (opt_.delay_ms > 0) {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait_for(lk, std::chrono::milliseconds(opt_.delay_ms), [&] { return cancelled_; });
            if (cancelled_) { err = "cancelled"; return false; }   // sticky, like a closed socket
        }
        if (index < 0 || index >= segment_count()) { err = "no such segment"; return false; }
        if (index == opt_.fail_at) { err = "read failed (test)"; return false; }
        const S &s = segs_[size_t(index) % segs_.size()];
        out.index = index;
        out.start_ms = int64_t(index / int(segs_.size())) * total_ + s.start;
        out.duration_ms = s.dur;
        out.bytes.assign(file_.begin() + std::ptrdiff_t(s.off), file_.begin() + std::ptrdiff_t(s.off + s.len));
        return true;
    }
    void cancel() override {
        { std::lock_guard<std::mutex> lk(m_); cancelled_ = true; }
        cv_.notify_all();
    }
    std::atomic<int> reads{0};

private:
    struct S { size_t off, len; int64_t start, dur; };
    bool index() {
        const std::span<const uint8_t> b(file_);
        size_t pos = 0;
        auto skip = [&](uint32_t want_id, bool &found, size_t &hdr_end, uint64_t &size) {
            size_t p = pos; uint32_t id; bool unk;
            if (!ebml_read_id(b, p, id) || !ebml_read_size(b, p, size, unk)) return false;
            found = id == want_id; hdr_end = p; return true;
        };
        // top level: EBML header, then Segment (descend), then its children up to the first Cluster
        bool found; size_t hdr; uint64_t size;
        if (!skip(0x1A45DFA3, found, hdr, size) || !found) return false;
        pos = hdr + size;
        if (!skip(0x18538067, found, hdr, size) || !found) return false;
        pos = hdr;
        while (pos < b.size()) {
            if (!skip(0x1F43B675, found, hdr, size)) return false;
            if (found) break;
            pos = hdr + size;
        }
        init_.assign(file_.begin(), file_.begin() + std::ptrdiff_t(pos));
        std::vector<S> out;
        while (pos < b.size()) {
            if (!skip(0x1F43B675, found, hdr, size) || !found) break;   // Cues etc. after the clusters
            S s{pos, hdr + size - pos, 0, 0};
            size_t q = hdr; uint32_t id; uint64_t tsz; bool unk;
            if (!ebml_read_id(b, q, id) || id != 0xE7 || !ebml_read_size(b, q, tsz, unk)) return false;
            for (uint64_t k = 0; k < tsz; k++) s.start = s.start << 8 | b[q + k];
            out.push_back(s);
            pos = hdr + size;
        }
        if (out.empty()) return false;
        for (size_t k = 0; k < out.size(); k++)
            out[k].dur = k + 1 < out.size() ? out[k + 1].start - out[k].start : (k ? out[k - 1].dur : 1000);
        // the container's Duration (0x4489, a float, in ms) says where the last cluster ends
        for (size_t k = 0; k + 6 < init_.size(); k++)
            if (init_[k] == 0x44 && init_[k + 1] == 0x89 && init_[k + 2] == 0x88) {
                uint64_t bits = 0;
                for (int j = 0; j < 8; j++) bits = bits << 8 | init_[k + 3 + size_t(j)];
                double d; std::memcpy(&d, &bits, 8);
                if (d > double(out.back().start)) out.back().dur = int64_t(d) - out.back().start;
                break;
            }
        total_ = out.back().start + out.back().dur;
        segs_ = std::move(out);
        return true;
    }
    Options opt_;
    std::vector<uint8_t> file_, init_;
    std::vector<S> segs_;
    int64_t total_ = 1;
    std::mutex m_;
    std::condition_variable cv_;
    bool cancelled_ = false;
};

// Keeps every sample written. realtime: each write takes as long as its audio lasts.
class CaptureSink : public Sink {
public:
    struct Shared {
        std::mutex m;
        std::vector<int16_t> pcm;
        int opens = 0, closes = 0, writes = 0, channels = 0;
        bool realtime = false;
        std::atomic<size_t> limit{SIZE_MAX};   // write() blocks while this many frames are in (a gate)
        std::atomic<int> blocked{0};           // writes waiting at the gate
        size_t frames() { std::lock_guard<std::mutex> l(m); return channels ? pcm.size() / size_t(channels) : 0; }
    };
    explicit CaptureSink(std::shared_ptr<Shared> s) : s_(std::move(s)) {}
    bool open(int rate, int channels, std::string &) override {
        rate_ = rate;
        std::lock_guard<std::mutex> l(s_->m);
        s_->opens++;
        s_->channels = channels;
        return true;
    }
    bool write(const int16_t *p, size_t frames, std::string &) override {
        bool rt;
        if (s_->frames() >= s_->limit) {
            s_->blocked++;
            while (s_->frames() >= s_->limit) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            s_->blocked--;
        }
        {
            std::lock_guard<std::mutex> l(s_->m);
            s_->pcm.insert(s_->pcm.end(), p, p + frames * size_t(s_->channels));
            s_->writes++;
            rt = s_->realtime;
        }
        if (rt) std::this_thread::sleep_for(std::chrono::microseconds(frames * 1000000 / size_t(rate_)));
        return true;
    }
    void close() override {
        std::lock_guard<std::mutex> l(s_->m);
        s_->closes++;
    }
private:
    std::shared_ptr<Shared> s_;
    int rate_ = 48000;
};

// Records the player's events; wait_* poll with generous timeouts (a slow machine is not a flake).
struct Events {
    std::mutex m;
    std::vector<PlayerEvent> v;
    std::vector<size_t> frames_at;   // sink frames when each event arrived
    std::shared_ptr<CaptureSink::Shared> sink;
    void attach(Player &p) {
        p.set_listener([this](const PlayerEvent &e) {
            const size_t f = sink ? sink->frames() : 0;
            std::lock_guard<std::mutex> l(m);
            v.push_back(e);
            frames_at.push_back(f);
        });
    }
    int count(PlayerEvent::Kind k) {
        std::lock_guard<std::mutex> l(m);
        int n = 0;
        for (auto &e : v) n += e.kind == k;
        return n;
    }
    std::vector<PlayerEvent> all() { std::lock_guard<std::mutex> l(m); return v; }
};

template <class F> bool wait_until(F f, int timeout_ms = 15000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!f()) {
        if (std::chrono::steady_clock::now() > end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}
inline bool wait_state(Player &p, PlayState s, int timeout_ms = 15000) {
    return wait_until([&] { return p.status().state == s; }, timeout_ms);
}

inline Track mk(const std::string &id, int dur_s = 0) {
    Track t; t.id = id; t.title = "title " + id; t.duration_s = dur_s; return t;
}

// A Player over the two tone files: track ids "a" (440 Hz, 12 s) and "b" (880 Hz, 8 s); any other
// id gets the 440 Hz file. `tweak(id, options)` lets a test add delays or failures; sources that
// were made are kept in `made` (alive for the test's duration).
struct Rig {
    std::shared_ptr<CaptureSink::Shared> sink = std::make_shared<CaptureSink::Shared>();
    Events ev;
    std::mutex m;
    std::vector<std::string> opened;   // ids passed to the factory, in order
    std::function<void(const std::string &, FileSource::Options &)> tweak;
    std::unique_ptr<Player> player;
    Rig() {
        ev.sink = sink;
        player = std::make_unique<Player>(
            [this](const Track &t, std::string &err) -> std::unique_ptr<Source> {
                FileSource::Options o;
                if (tweak) tweak(t.id, o);
                { std::lock_guard<std::mutex> l(m); opened.push_back(t.id); }
                auto s = std::make_unique<FileSource>(fixture(t.id == "b" ? "tone880_8s.webm" : "tone440_12s.webm"), o);
                if (!s->open(err)) return nullptr;
                return s;
            },
            [this]() -> std::unique_ptr<Sink> { return std::make_unique<CaptureSink>(sink); });
        ev.attach(*player);
    }
    ~Rig() { sink->limit = SIZE_MAX; player.reset(); }
    // Let the sink take `frames` and hold the next write; true once the player is waiting there.
    bool gate_at(size_t frames) {
        sink->limit = frames;
        return wait_until([&] { return sink->blocked > 0; });
    }
    void release() { sink->limit = SIZE_MAX; }
    int states(PlayState s) {
        int n = 0;
        for (auto &e : ev.all()) n += e.kind == PlayerEvent::State && e.status.state == s;
        return n;
    }
    // Frames in the sink when the Position event for exactly `ms` arrived (the seek's own).
    size_t frames_at_position(int64_t ms) {
        std::lock_guard<std::mutex> l(ev.m);
        for (size_t i = 0; i < ev.v.size(); i++)
            if (ev.v[i].kind == PlayerEvent::Position && ev.v[i].status.position_ms == ms) return ev.frames_at[i];
        return SIZE_MAX;
    }
    Player *operator->() { return player.get(); }
};

// Zero crossings of the left channel in frames [from, to).
inline int crossings(const std::vector<int16_t> &pcm, int ch, size_t from, size_t to) {
    int n = 0;
    for (size_t i = from + 1; i < to; i++) n += (pcm[(i - 1) * size_t(ch)] < 0) != (pcm[i * size_t(ch)] < 0);
    return n;
}

}  // namespace ptest
