// parse_webm_index: byte 0 is spread into content_length and the fallback duration.
#include <cstdint>
#include <span>
#include <string>

#include "fuzz.h"
#include "stream/webm_index.h"

using namespace bbm;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) return 0;
    const int64_t content_length = int64_t(data[0]) * 4096 + data[1] * 16 + (size - 2) * (data[0] & 1);
    const int64_t fallback = int64_t(data[1]) * 1000;
    WebmIndex idx;
    std::string err;
    if (!parse_webm_index({data + 2, size - 2}, content_length, fallback, idx, err)) {
        FUZZ_ASSERT(!err.empty());
        return 0;
    }
    int64_t prev_end = 0;
    for (size_t i = 0; i < idx.segments.size(); i++) {
        const SegmentRange &s = idx.segments[i];
        FUZZ_ASSERT(s.offset >= 0 && s.size >= 0);
        FUZZ_ASSERT(s.offset >= prev_end);                         // in order, no overlap
        FUZZ_ASSERT(s.offset + s.size <= content_length);          // inside the stated length
        FUZZ_ASSERT(s.start_ms >= 0 && s.duration_ms >= 0);
        prev_end = s.offset + s.size;
    }
    const int n = int(idx.segments.size());
    for (int64_t ms : {int64_t(-5), int64_t(0), idx.duration_ms, idx.duration_ms * 2}) {
        const int k = idx.segment_at(ms);
        FUZZ_ASSERT(n == 0 ? k == -1 : (k >= 0 && k < n));
    }
    return 0;
}
