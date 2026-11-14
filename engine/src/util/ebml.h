// EBML element ids and sizes, bounds-checked: never reads past the span.
// Ids keep their length marker; sizes drop it. An all-ones size means "unknown".
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace bbm {

inline bool ebml_read_id(std::span<const uint8_t> b, size_t &pos, uint32_t &id) {
    if (pos >= b.size() || b[pos] == 0) return false;
    size_t n = 1;
    for (uint8_t mask = 0x80; !(b[pos] & mask); mask >>= 1) n++;
    if (n > 4 || n > b.size() - pos) return false;
    id = 0;
    for (size_t i = 0; i < n; i++) id = id << 8 | b[pos++];
    return true;
}

inline bool ebml_read_size(std::span<const uint8_t> b, size_t &pos, uint64_t &size, bool &unknown) {
    if (pos >= b.size() || b[pos] == 0) return false;
    size_t n = 1;
    uint8_t mask = 0x80;
    for (; !(b[pos] & mask); mask >>= 1) n++;
    if (n > b.size() - pos) return false;
    size = b[pos++] & (mask - 1);
    bool ones = size == uint64_t(mask - 1);
    for (size_t i = 1; i < n; i++) {
        ones = ones && b[pos] == 0xFF;
        size = size << 8 | b[pos++];
    }
    unknown = ones;
    return true;
}

}  // namespace bbm
