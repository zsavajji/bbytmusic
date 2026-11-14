#include "util/base64.h"

namespace bbm {
namespace {
int value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

std::string encode(const uint8_t *d, size_t n, const char *tbl, bool pad) {
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        out += tbl[v >> 18 & 63];
        out += tbl[v >> 12 & 63];
        if (i + 1 < n) out += tbl[v >> 6 & 63]; else if (pad) out += '=';
        if (i + 2 < n) out += tbl[v & 63]; else if (pad) out += '=';
    }
    return out;
}
}  // namespace

bool base64_decode(std::string_view text, std::vector<uint8_t> &out) {
    out.clear();
    while (!text.empty() && text.back() == '=') text.remove_suffix(1);
    if (text.size() % 4 == 1) return false;
    uint32_t acc = 0;
    int bits = 0;
    for (const char c : text) {
        const int v = value(c);
        if (v < 0) { out.clear(); return false; }
        acc = acc << 6 | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(acc >> bits));
            acc &= (1u << bits) - 1;
        }
    }
    return true;
}

std::string base64_encode(const uint8_t *d, size_t n) {
    return encode(d, n, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", true);
}
std::string base64url_encode(const uint8_t *d, size_t n, bool pad) {
    return encode(d, n, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_", pad);
}
}  // namespace bbm
