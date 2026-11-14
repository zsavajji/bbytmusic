// The segment table of a WebM file, from its head: the bytes from 0 through the end of the Cues.
// Segment i is cluster i: from cue i's byte offset up to cue i+1's (the last one up to the end
// of the file), so head + every segment in order is the whole file.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace bbm {

struct SegmentRange {
    int64_t start_ms = 0, duration_ms = 0;
    int64_t offset = 0, size = 0;   // bytes, absolute in the file
};

struct WebmIndex {
    int64_t duration_ms = 0;               // Info's Duration, else the fallback given
    std::vector<SegmentRange> segments;
    int segment_at(int64_t ms) const;      // the segment that holds ms, clamped; -1 if none
};

// fallback_duration_ms (the /player's approxDurationMs) is used when Info has no Duration.
bool parse_webm_index(std::span<const uint8_t> head, int64_t content_length, int64_t fallback_duration_ms,
                      WebmIndex &out, std::string &err);

}  // namespace bbm
