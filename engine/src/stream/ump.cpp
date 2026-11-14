#include "stream/ump.h"

namespace bbm {

bool ump_read_varint(std::span<const uint8_t> b, size_t &pos, uint32_t &v) {
    if (pos >= b.size()) return false;
    uint8_t b0 = b[pos];
    size_t n = b0 < 0x80 ? 1 : b0 < 0xC0 ? 2 : b0 < 0xE0 ? 3 : b0 < 0xF0 ? 4 : 5;
    if (n > b.size() - pos) return false;
    const uint8_t *p = b.data() + pos;
    switch (n) {
    case 1: v = b0; break;
    case 2: v = (b0 & 0x3Fu) + 64u * p[1]; break;
    case 3: v = (b0 & 0x1Fu) + 32u * (p[1] + 256u * p[2]); break;
    case 4: v = (b0 & 0x0Fu) + 16u * (p[1] + 256u * (p[2] + 256u * p[3])); break;
    default: v = p[1] | uint32_t(p[2]) << 8 | uint32_t(p[3]) << 16 | uint32_t(p[4]) << 24; break;
    }
    pos += n;
    return true;
}

void ump_write_varint(std::vector<uint8_t> &out, uint32_t v) {
    if (v < (1u << 7)) {
        out.push_back(uint8_t(v));
    } else if (v < (1u << 14)) {
        out.insert(out.end(), {uint8_t(0x80 | (v & 0x3F)), uint8_t(v >> 6)});
    } else if (v < (1u << 21)) {
        out.insert(out.end(), {uint8_t(0xC0 | (v & 0x1F)), uint8_t(v >> 5), uint8_t(v >> 13)});
    } else if (v < (1u << 28)) {
        out.insert(out.end(), {uint8_t(0xE0 | (v & 0x0F)), uint8_t(v >> 4), uint8_t(v >> 12), uint8_t(v >> 20)});
    } else {
        out.insert(out.end(), {uint8_t(0xF0), uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)});
    }
}

void ump_write_part(std::vector<uint8_t> &out, uint32_t type, std::span<const uint8_t> payload) {
    ump_write_varint(out, type);
    ump_write_varint(out, uint32_t(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

void UmpReader::feed(std::span<const uint8_t> chunk) {
    if (pos_ > 0) {  // drop the parts already handed out
        buf_.erase(buf_.begin(), buf_.begin() + long(pos_));
        pos_ = 0;
    }
    buf_.insert(buf_.end(), chunk.begin(), chunk.end());
}

bool UmpReader::next(uint32_t &type, std::span<const uint8_t> &payload) {
    std::span<const uint8_t> b(buf_);
    size_t p = pos_;
    uint32_t t, size;
    if (!ump_read_varint(b, p, t) || !ump_read_varint(b, p, size) || size > b.size() - p) return false;
    type = t;
    payload = b.subspan(p, size);
    pos_ = p + size;
    return true;
}

}  // namespace bbm
