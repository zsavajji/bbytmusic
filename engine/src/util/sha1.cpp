#include "util/sha1.h"

#include <cstring>

namespace bbm {
namespace {
uint32_t rol(uint32_t v, int n) { return v << n | v >> (32 - n); }

void block(uint32_t h[5], const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        const uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}
}  // namespace

std::string sha1_hex(const uint8_t *data, size_t size) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    size_t i = 0;
    for (; i + 64 <= size; i += 64) block(h, data + i);
    uint8_t tail[128] = {};
    const size_t rest = size - i;
    if (rest) std::memcpy(tail, data + i, rest);
    tail[rest] = 0x80;
    const size_t len = rest < 56 ? 64 : 128;
    const uint64_t bits = uint64_t(size) * 8;
    for (int k = 0; k < 8; k++) tail[len - 1 - k] = uint8_t(bits >> (8 * k));
    block(h, tail);
    if (len == 128) block(h, tail + 64);
    static const char hex[] = "0123456789abcdef";
    std::string out;
    for (uint32_t v : h)
        for (int s = 28; s >= 0; s -= 4) out += hex[v >> s & 15];
    return out;
}
}  // namespace bbm
