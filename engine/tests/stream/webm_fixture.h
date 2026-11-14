// A synthetic WebM file for the stream tests: an EBML header, a Segment with Info, Tracks and
// Cues, then one Cluster per cue with filler bytes. Not playable audio; just the right layout.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace webm_fixture {
using Bytes = std::vector<uint8_t>;

inline void put_id(Bytes &o, uint32_t id) {
    for (int s = 24; s >= 0; s -= 8)
        if (id >> s || s == 0) o.push_back(uint8_t(id >> s));
}
inline Bytes elem(uint32_t id, const Bytes &body) {  // size as 8 bytes, as muxers often write it
    Bytes o;
    put_id(o, id);
    o.push_back(0x01);
    for (int s = 48; s >= 0; s -= 8) o.push_back(uint8_t(uint64_t(body.size()) >> s));
    o.insert(o.end(), body.begin(), body.end());
    return o;
}
inline Bytes uint_elem(uint32_t id, uint64_t v, int width = 4) {
    Bytes b;
    for (int s = 8 * (width - 1); s >= 0; s -= 8) b.push_back(uint8_t(v >> s));
    return elem(id, b);
}
inline Bytes float_elem(uint32_t id, double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    return uint_elem(id, bits, 8);
}
inline Bytes cat(std::initializer_list<Bytes> parts) {
    Bytes o;
    for (const Bytes &p : parts) o.insert(o.end(), p.begin(), p.end());
    return o;
}

struct File {
    Bytes head;                      // through the end of the Cues
    std::vector<Bytes> clusters;     // the segments, in order
    Bytes whole() const {
        Bytes o = head;
        for (const Bytes &c : clusters) o.insert(o.end(), c.begin(), c.end());
        return o;
    }
};

// Cue times in ticks of `scale` ns; cluster i is cluster_size[i] bytes. duration_ticks < 0: no Duration.
inline File make(const std::vector<uint64_t> &cue_ticks, const std::vector<size_t> &cluster_size,
                 double duration_ticks, uint64_t scale = 1000000) {
    Bytes ebml = elem(0x1A45DFA3, cat({elem(0x4282, {'w', 'e', 'b', 'm'})}));
    Bytes info_body = uint_elem(0x2AD7B1, scale);
    if (duration_ticks >= 0) info_body = cat({info_body, float_elem(0x4489, duration_ticks)});
    Bytes front = cat({elem(0x114D9B74, Bytes(20, 0)), elem(0xEC, Bytes(7, 0)), elem(0x1549A966, info_body),
                       elem(0x1654AE6B, cat({elem(0xAE, cat({uint_elem(0xD7, 1, 1)}))}))});
    Bytes seg_hdr{0x18, 0x53, 0x80, 0x67, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // unknown size
    auto cues_for = [&](size_t first_rel) {
        Bytes points;
        size_t rel = first_rel;
        for (size_t i = 0; i < cue_ticks.size(); i++) {
            Bytes tp = cat({uint_elem(0xF7, 1, 1), uint_elem(0xF1, rel, 4)});
            points = cat({points, elem(0xBB, cat({uint_elem(0xB3, cue_ticks[i], 4), elem(0xB7, tp)}))});
            rel += cluster_size[i];
        }
        return elem(0x1C53BB6B, points);
    };
    size_t cues_len = cues_for(0).size();  // positions are 4 bytes wide, so the length is fixed
    File f;
    f.head = cat({ebml, seg_hdr, front, cues_for(front.size() + cues_len)});
    for (size_t i = 0; i < cluster_size.size(); i++) {
        Bytes c = elem(0x1F43B675, Bytes(cluster_size[i] - 12, uint8_t(i + 1)));  // 4 id + 8 size
        f.clusters.push_back(c);
    }
    return f;
}
}  // namespace webm_fixture
