#include "check.h"
#include "stream/webm_fixture.h"
#include "stream/webm_index.h"

#include <string>

using namespace bbm;
using webm_fixture::Bytes;

static void basic() {
    // three 10 s clusters and a 3.5 s one; Duration 33 500 ticks of 1 ms
    auto f = webm_fixture::make({0, 10000, 20000, 30000}, {1000, 1200, 900, 300}, 33500.0);
    Bytes file = f.whole();
    WebmIndex idx;
    std::string err;
    CHECK(parse_webm_index(f.head, int64_t(file.size()), 99999, idx, err));
    CHECK_EQ(err, "");
    CHECK_EQ(idx.duration_ms, 33500);
    CHECK_EQ(idx.segments.size(), size_t(4));
    if (idx.segments.size() != 4) return;
    int64_t at = int64_t(f.head.size());
    for (size_t i = 0; i < 4; i++) {
        const SegmentRange &s = idx.segments[i];
        CHECK_EQ(s.offset, at);
        CHECK_EQ(s.size, int64_t(f.clusters[i].size()));
        // the range is exactly cluster i
        CHECK(Bytes(file.begin() + s.offset, file.begin() + s.offset + s.size) == f.clusters[i]);
        CHECK_EQ(s.start_ms, int64_t(i) * 10000);
        at += s.size;
    }
    CHECK_EQ(at, int64_t(file.size()));
    CHECK_EQ(idx.segments[0].duration_ms, 10000);
    CHECK_EQ(idx.segments[3].duration_ms, 3500);
    CHECK_EQ(idx.segment_at(-5), 0);
    CHECK_EQ(idx.segment_at(0), 0);
    CHECK_EQ(idx.segment_at(9999), 0);
    CHECK_EQ(idx.segment_at(10000), 1);
    CHECK_EQ(idx.segment_at(29999), 2);
    CHECK_EQ(idx.segment_at(33000), 3);
    CHECK_EQ(idx.segment_at(1000000), 3);
    CHECK_EQ(WebmIndex{}.segment_at(0), -1);
}

static void scales_and_fallbacks() {
    std::string err;
    WebmIndex idx;
    // TimecodeScale 100 us: ticks are tenths of a ms
    auto f = webm_fixture::make({0, 50000}, {500, 500}, 120000.0, 100000);
    CHECK(parse_webm_index(f.head, int64_t(f.whole().size()), 0, idx, err));
    CHECK(idx.segments.size() == 2 && idx.segments[1].start_ms == 5000);
    CHECK_EQ(idx.duration_ms, 12000);
    // no Duration: the fallback
    auto g = webm_fixture::make({0, 10000}, {500, 500}, -1);
    CHECK(parse_webm_index(g.head, int64_t(g.whole().size()), 17000, idx, err));
    CHECK_EQ(idx.duration_ms, 17000);
    CHECK_EQ(idx.segments[1].duration_ms, 7000);
    // no Duration, no fallback: the last segment is empty, never negative
    CHECK(parse_webm_index(g.head, int64_t(g.whole().size()), 0, idx, err));
    CHECK_EQ(idx.duration_ms, 10000);
    CHECK_EQ(idx.segments[1].duration_ms, 0);
}

static void bad_input() {
    auto f = webm_fixture::make({0, 10000, 20000}, {1000, 1000, 1000}, 25000.0);
    int64_t len = int64_t(f.whole().size());
    WebmIndex idx;
    std::string err;
    // every truncation of the head fails: the Cues are its last element
    bool all = true;
    for (size_t n = 0; n < f.head.size(); n++) {
        Bytes cut(f.head.begin(), f.head.begin() + long(n));
        all = all && !parse_webm_index(cut, len, 0, idx, err) && !err.empty();
    }
    CHECK(all);
    // the last cluster starts past the end of the file
    CHECK(!parse_webm_index(f.head, len - 1000, 0, idx, err));
    CHECK(!parse_webm_index(f.head, 0, 0, idx, err));
    // cue offsets that go backwards
    auto g = webm_fixture::make({0, 10000}, {1000, 1000}, 20000.0);
    Bytes h = g.head;
    // the last CueClusterPosition's 4 bytes are the last 4 of the head: point it at cluster 0
    size_t rel0 = size_t(h[h.size() - 4]) << 24 | size_t(h[h.size() - 3]) << 16 | size_t(h[h.size() - 2]) << 8 |
                  h[h.size() - 1];
    rel0 -= 1000;
    for (int i = 0; i < 4; i++) h[h.size() - 4 + i] = uint8_t(rel0 >> (24 - 8 * i));
    CHECK(!parse_webm_index(h, int64_t(g.whole().size()), 0, idx, err));
    // not WebM at all
    CHECK(!parse_webm_index(Bytes{0x00, 0x00, 0x00, 0x20, 'f', 't', 'y', 'p'}, 100, 0, idx, err));
    CHECK(!parse_webm_index(Bytes{}, 100, 0, idx, err));
    // every single-byte corruption: no crash, no read out of bounds (ASan), any answer
    for (size_t i = 0; i < f.head.size(); i++)
        for (uint8_t x : {uint8_t(0x00), uint8_t(0xFF), uint8_t(0x80), uint8_t(0x01)}) {
            Bytes c = f.head;
            c[i] = x;
            if (parse_webm_index(c, len, 0, idx, err)) {
                for (const SegmentRange &s : idx.segments) CHECK(s.offset >= 0 && s.size > 0 && s.offset + s.size <= len);
            }
        }
}

int main() {
    basic();
    scales_and_fallbacks();
    bad_input();
    return check_done();
}
