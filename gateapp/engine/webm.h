// The WebM (EBML) walk of tools/gates/gate04.cpp: the Opus packets of a file and its header.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct Webm {
    struct Packet { size_t offset, size; };
    std::vector<uint8_t> bytes;
    std::vector<Packet> packets;
    int channels = 2;
    int pre_skip = 0;
};

namespace webm_detail {
inline bool read_id(const std::vector<uint8_t> &b, size_t &pos, uint32_t &id) {
    if (pos >= b.size() || b[pos] == 0) return false;
    int n = 1;
    for (uint8_t mask = 0x80; !(b[pos] & mask); mask >>= 1) n++;
    if (n > 4 || pos + n > b.size()) return false;
    id = 0;
    for (int i = 0; i < n; i++) id = id << 8 | b[pos++];
    return true;
}
inline bool read_size(const std::vector<uint8_t> &b, size_t &pos, uint64_t &size, bool &unknown) {
    if (pos >= b.size() || b[pos] == 0) return false;
    int n = 1;
    uint8_t mask = 0x80;
    for (; !(b[pos] & mask); mask >>= 1) n++;
    if (pos + n > b.size()) return false;
    size = b[pos++] & (mask - 1);
    bool ones = size == uint64_t(mask - 1);
    for (int i = 1; i < n; i++) {
        ones = ones && b[pos] == 0xFF;
        size = size << 8 | b[pos++];
    }
    unknown = ones;
    return true;
}
}  // namespace webm_detail

// Reads and parses `path`; false with `why` set on failure.
inline bool webm_load(const std::string &path, Webm &w, std::string &why) {
    using namespace webm_detail;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) { why = "cannot open " + path; return false; }
    std::fseek(f, 0, SEEK_END);
    w.bytes.resize(size_t(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    const bool read_ok = std::fread(w.bytes.data(), 1, w.bytes.size(), f) == w.bytes.size();
    std::fclose(f);
    if (!read_ok || w.bytes.empty()) { why = "cannot read " + path; return false; }
    const auto &b = w.bytes;
    size_t pos = 0;
    while (pos < b.size()) {
        uint32_t id;
        uint64_t size;
        bool unknown;
        if (!read_id(b, pos, id) || !read_size(b, pos, size, unknown)) { why = "bad EBML header"; return false; }
        switch (id) {
        case 0x18538067: case 0x1F43B675: case 0x1654AE6B: case 0xAE: case 0xA0:
            continue;  // Segment, Cluster, Tracks, TrackEntry, BlockGroup: descend
        case 0x63A2:  // CodecPrivate: OpusHead
            if (size >= 12 && pos + 12 <= b.size() && std::memcmp(&b[pos], "OpusHead", 8) == 0) {
                w.channels = b[pos + 9];
                w.pre_skip = b[pos + 10] | b[pos + 11] << 8;
            }
            break;
        case 0xA3: case 0xA1:  // SimpleBlock, Block: track (1 byte), timecode (2), flags (1), frame
            if (size > 4 && pos + size <= b.size()) w.packets.push_back({pos + 4, size_t(size) - 4});
            break;
        default: break;
        }
        if (unknown || pos + size > b.size()) { why = "bad EBML element"; return false; }
        pos += size;
    }
    if (w.packets.empty()) { why = "no packets"; return false; }
    if (w.channels < 1 || w.channels > 2) { why = "unsupported channel count"; return false; }
    return true;
}
