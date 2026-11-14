#include "media/webm_demux.h"

#include <algorithm>
#include <cstring>

#include "util/ebml.h"

namespace bbm {
namespace {
constexpr size_t kMaxLeaf = 16u << 20;      // largest element we buffer whole
constexpr size_t kMaxBuffer = 64u << 20;    // bound on fed-but-unparsed bytes
constexpr size_t kMaxHeader = 12;           // 4 id bytes + 8 size bytes

enum : uint32_t {
    kSegment = 0x18538067, kInfo = 0x1549A966, kTracks = 0x1654AE6B, kTrackEntry = 0xAE,
    kAudio = 0xE1, kCluster = 0x1F43B675, kBlockGroup = 0xA0,
    kTimecodeScale = 0x2AD7B1, kTimecode = 0xE7, kCodecPrivate = 0x63A2,
    kCodecDelay = 0x56AA, kSeekPreRoll = 0x56BB, kSimpleBlock = 0xA3, kBlock = 0xA1,
};

bool is_master(uint32_t id) {
    return id == kSegment || id == kInfo || id == kTracks || id == kTrackEntry || id == kAudio ||
           id == kCluster || id == kBlockGroup;
}
bool is_leaf_wanted(uint32_t id) {
    return id == kTimecodeScale || id == kTimecode || id == kCodecPrivate || id == kCodecDelay ||
           id == kSeekPreRoll || id == kSimpleBlock || id == kBlock;
}
bool read_uint(const uint8_t *p, size_t n, uint64_t &v) {
    if (n == 0 || n > 8) return false;
    v = 0;
    for (size_t i = 0; i < n; i++) v = v << 8 | p[i];
    return true;
}
}  // namespace

void WebmDemuxer::compact() {
    if (pos_ == 0) return;
    // next() comes here for every packet: moving a whole segment's tail down 50 times a second
    // is wasted work, so wait until a good piece is used up (or everything is).
    if (pos_ < buf_.size() && pos_ < (64u << 10)) return;
    buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(pos_));
    pos_ = 0;
}

void WebmDemuxer::reset() {
    buf_.clear();
    pos_ = 0;
    skip_ = 0;
    cluster_tc_ = 0;
    error_.clear();
}

bool WebmDemuxer::feed(std::span<const uint8_t> bytes, std::string &err) {
    if (!error_.empty()) { err = error_; return false; }
    compact();
    if (buf_.size() + bytes.size() > kMaxBuffer) { fail("webm: buffer overflow"); err = error_; return false; }
    buf_.insert(buf_.end(), bytes.begin(), bytes.end());
    // Parse up to the first packet so has_header() and the accessors are current.
    Packet unused;
    bool produced = false;
    while (step(unused, produced, false)) {}
    if (!error_.empty()) { err = error_; return false; }
    return true;
}

bool WebmDemuxer::next(Packet &out) {
    compact();
    if (!error_.empty()) return false;
    for (;;) {
        bool produced = false;
        if (!step(out, produced, true)) return false;
        if (produced) return true;
    }
}

// Parses one element at pos_. Returns false when more bytes are needed or on error.
bool WebmDemuxer::step(Packet &out, bool &produced, bool take_blocks) {
    if (skip_) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(skip_, buf_.size() - pos_));
        pos_ += n;
        skip_ -= n;
        return skip_ == 0 && pos_ < buf_.size();
    }
    if (pos_ >= buf_.size()) return false;
    std::span<const uint8_t> all(buf_);
    size_t p = pos_;
    uint32_t id = 0;
    uint64_t size = 0;
    bool unknown = false;
    if (!ebml_read_id(all, p, id) || !ebml_read_size(all, p, size, unknown)) {
        if (buf_.size() - pos_ >= kMaxHeader) fail("webm: bad element header");
        return false;
    }
    if (is_master(id)) { pos_ = p; return true; }    // descend; unknown size is fine here
    if (unknown) { fail("webm: unknown size on a non-master element"); return false; }
    if (!is_leaf_wanted(id)) {
        pos_ = p;
        skip_ = size;
        return true;
    }
    if (!take_blocks && (id == kSimpleBlock || id == kBlock)) return false;   // leave packets for next()
    if (size > kMaxLeaf) { fail("webm: element too large"); return false; }
    if (buf_.size() - p < size) return false;       // wait for the rest
    const uint8_t *d = buf_.data() + p;
    const size_t n = static_cast<size_t>(size);
    uint64_t v = 0;
    switch (id) {
    case kTimecodeScale:
        if (read_uint(d, n, v) && v > 0 && v < (1ull << 40)) scale_ns_ = static_cast<int64_t>(v);
        break;
    case kTimecode:
        if (read_uint(d, n, v) && v < (1ull << 52)) cluster_tc_ = static_cast<int64_t>(v);
        break;
    case kCodecDelay:
        if (read_uint(d, n, v) && v < (1ull << 52)) codec_delay_ns_ = static_cast<int64_t>(v);
        break;
    case kSeekPreRoll:
        if (read_uint(d, n, v) && v < (1ull << 52)) seek_preroll_ns_ = static_cast<int64_t>(v);
        break;
    case kCodecPrivate:
        if (n >= 12 && std::memcmp(d, "OpusHead", 8) == 0 && d[9] >= 1 && d[9] <= 2) {
            channels_ = d[9];
            pre_skip_ = d[10] | d[11] << 8;
            have_header_ = true;
        }
        break;
    default: {  // SimpleBlock / Block: track vint, int16 timecode, flags, frame
        size_t q = 0;
        uint64_t track = 0;
        bool unk = false;
        std::span<const uint8_t> blk(d, n);
        if (!ebml_read_size(blk, q, track, unk) || unk || track != 1 || n < q + 3) break;
        const int16_t rel = static_cast<int16_t>(d[q] << 8 | d[q + 1]);
        const uint8_t flags = d[q + 2];
        q += 3;
        if ((flags & 0x06) != 0 || q >= n) break;   // laced or empty: Opus in WebM is neither
        const int64_t ticks = cluster_tc_ + rel;
        // ticks * scale_ns_ / 1e6 without overflow: ticks < 2^53, scale < 2^40 -> use __int128-free split
        const int64_t whole = ticks / 1000000 * scale_ns_ + ticks % 1000000 * scale_ns_ / 1000000;
        out.pts_ms = whole;
        out.data = d + q;
        out.size = n - q;
        produced = true;
        break;
    }
    }
    pos_ = p + n;
    return true;
}

}  // namespace bbm
