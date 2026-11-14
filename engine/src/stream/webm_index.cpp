#include "stream/webm_index.h"

#include <algorithm>
#include <cstring>

#include "util/ebml.h"

namespace bbm {
namespace {

constexpr uint32_t kSegment = 0x18538067, kInfo = 0x1549A966, kCues = 0x1C53BB6B, kCluster = 0x1F43B675;
constexpr uint32_t kTimecodeScale = 0x2AD7B1, kDuration = 0x4489;
constexpr uint32_t kCuePoint = 0xBB, kCueTime = 0xB3, kCueTrackPositions = 0xB7, kCueClusterPosition = 0xF1;

using Bytes = std::span<const uint8_t>;

// Calls f(id, body) for each child of `body`; false if a child is malformed or overruns it.
template <class F> bool children(Bytes body, F &&f) {
    size_t pos = 0;
    while (pos < body.size()) {
        uint32_t id;
        uint64_t size;
        bool unknown;
        if (!ebml_read_id(body, pos, id) || !ebml_read_size(body, pos, size, unknown)) return false;
        if (unknown || size > body.size() - pos) return false;
        if (!f(id, body.subspan(pos, size_t(size)))) return false;
        pos += size_t(size);
    }
    return true;
}

bool be_uint(Bytes b, uint64_t &v) {
    if (b.size() > 8) return false;
    v = 0;
    for (uint8_t c : b) v = v << 8 | c;
    return true;
}

bool be_float(Bytes b, double &v) {
    uint64_t bits;
    if ((b.size() != 4 && b.size() != 8) || !be_uint(b, bits)) return false;
    if (b.size() == 4) {
        uint32_t b32 = uint32_t(bits);
        float f;
        std::memcpy(&f, &b32, 4);
        v = f;
    } else {
        std::memcpy(&v, &bits, 8);
    }
    return true;
}

struct RawCue { uint64_t ticks = 0, offset = 0; };

bool parse_cue_point(Bytes body, RawCue &cue) {
    bool has_time = false, has_pos = false;
    bool ok = children(body, [&](uint32_t id, Bytes b) {
        if (id == kCueTime) has_time = be_uint(b, cue.ticks);
        if (id == kCueTrackPositions && !has_pos)
            return children(b, [&](uint32_t cid, Bytes cb) {
                if (cid == kCueClusterPosition) has_pos = be_uint(cb, cue.offset);
                return true;
            });
        return true;
    });
    return ok && has_time && has_pos;
}

}  // namespace

int WebmIndex::segment_at(int64_t ms) const {
    if (segments.empty()) return -1;
    auto it = std::upper_bound(segments.begin(), segments.end(), ms,
                               [](int64_t t, const SegmentRange &s) { return t < s.start_ms; });
    return it == segments.begin() ? 0 : int(it - segments.begin()) - 1;
}

bool parse_webm_index(Bytes head, int64_t content_length, int64_t fallback_duration_ms, WebmIndex &out,
                      std::string &err) {
    out = {};
    int64_t seg_start = -1;
    uint64_t scale = 1000000;  // ns per tick
    double duration_ticks = 0;
    std::vector<RawCue> cues;
    bool have_cues = false;
    size_t pos = 0;
    while (pos < head.size()) {
        uint32_t id;
        uint64_t size;
        bool unknown;
        if (!ebml_read_id(head, pos, id) || !ebml_read_size(head, pos, size, unknown)) {
            err = "webm: bad element header at " + std::to_string(pos);
            return false;
        }
        if (id == kSegment) {  // descend; its size runs to the end of the file, past the head
            seg_start = int64_t(pos);
            continue;
        }
        if (id == kCluster) break;
        if (unknown || size > head.size() - pos) {
            err = "webm: element past the end of the head at " + std::to_string(pos);
            return false;
        }
        Bytes body = head.subspan(pos, size_t(size));
        bool ok = true;
        if (id == kInfo) {
            ok = children(body, [&](uint32_t cid, Bytes b) {
                if (cid == kTimecodeScale) return be_uint(b, scale);
                if (cid == kDuration) return be_float(b, duration_ticks);
                return true;
            });
        } else if (id == kCues && seg_start >= 0) {
            have_cues = true;
            ok = children(body, [&](uint32_t cid, Bytes b) {
                if (cid != kCuePoint) return true;
                RawCue c;
                if (!parse_cue_point(b, c)) return false;
                cues.push_back(c);
                return true;
            });
        }
        if (!ok) {
            err = "webm: malformed element at " + std::to_string(pos);
            return false;
        }
        pos += size_t(size);
    }
    if (!have_cues || cues.empty()) {
        err = "webm: no Cues in the head";
        return false;
    }
    if (scale == 0 || scale > 1000000000) {
        err = "webm: bad TimecodeScale";
        return false;
    }
    if (content_length <= 0) {
        err = "webm: no content length";
        return false;
    }
    const uint64_t max_ticks = uint64_t(INT64_MAX) / scale;  // keeps ticks * scale in range
    int64_t prev_offset = int64_t(head.size()) - 1, prev_ms = 0;
    for (const RawCue &c : cues) {
        if (c.ticks > max_ticks || c.offset >= uint64_t(content_length)) {
            err = "webm: cue out of range";
            return false;
        }
        int64_t offset = seg_start + int64_t(c.offset);
        int64_t ms = int64_t(c.ticks * scale / 1000000);
        if (offset <= prev_offset || offset >= content_length || ms < prev_ms) {
            err = "webm: cues out of order";
            return false;
        }
        out.segments.push_back({ms, 0, offset, 0});
        prev_offset = offset;
        prev_ms = ms;
    }
    if (duration_ticks > 0 && duration_ticks < 1e15)
        out.duration_ms = int64_t(duration_ticks * double(scale) / 1e6 + 0.5);
    else
        out.duration_ms = fallback_duration_ms;
    out.duration_ms = std::max(out.duration_ms, out.segments.back().start_ms);
    for (size_t i = 0; i < out.segments.size(); i++) {
        SegmentRange &s = out.segments[i];
        bool last = i + 1 == out.segments.size();
        int64_t end_offset = last ? content_length : out.segments[i + 1].offset;
        int64_t end_ms = last ? out.duration_ms : out.segments[i + 1].start_ms;
        s.size = end_offset - s.offset;
        s.duration_ms = end_ms - s.start_ms;
    }
    return true;
}

}  // namespace bbm
