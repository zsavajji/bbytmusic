#include "check.h"
#include "stream/ump.h"

#include <string>
#include <utility>
#include <vector>

using namespace bbm;
using Bytes = std::vector<uint8_t>;
using Parts = std::vector<std::pair<uint32_t, Bytes>>;

static uint32_t read_one(const Bytes &b, size_t &pos, bool &ok) {
    uint32_t v = 0;
    ok = ump_read_varint(b, pos, v);
    return v;
}

static void varints() {
    // UmpParserTests.cs: 42 -> one byte; 80 02 -> (0x80 & 0x3F) + 64 * 2 = 128
    bool ok;
    size_t pos = 0;
    CHECK_EQ(read_one({42}, pos, ok), 42u);
    CHECK(ok && pos == 1);
    pos = 0;
    CHECK_EQ(read_one({0x80, 0x02}, pos, ok), 128u);
    CHECK(ok && pos == 2);
    // fetch_probe.py's ump_varint, one example per length
    pos = 0;
    CHECK_EQ(read_one({0xC5, 0x01, 0x02}, pos, ok), 5u + 32u * (1 + 256 * 2));
    pos = 0;
    CHECK_EQ(read_one({0xE7, 0x01, 0x02, 0x03}, pos, ok), 7u + 16u * (1 + 256 * (2 + 256 * 3)));
    pos = 0;
    CHECK_EQ(read_one({0xF0, 0x78, 0x56, 0x34, 0x12}, pos, ok), 0x12345678u);
    CHECK(ok && pos == 5);
    // round trip at every length boundary
    for (uint32_t v : {0u, 1u, 127u, 128u, 16383u, 16384u, 2097151u, 2097152u, 268435455u, 268435456u,
                       0xFFFFFFFFu}) {
        Bytes b;
        ump_write_varint(b, v);
        size_t expect = v < 128 ? 1 : v < 16384 ? 2 : v < 2097152 ? 3 : v < 268435456 ? 4 : 5;
        CHECK_EQ(b.size(), expect);
        pos = 0;
        CHECK_EQ(read_one(b, pos, ok), v);
        CHECK(ok && pos == b.size());
        // cut short: fails, pos untouched
        for (size_t n = 0; n < b.size(); n++) {
            Bytes cut(b.begin(), b.begin() + long(n));
            pos = 0;
            read_one(cut, pos, ok);
            CHECK(!ok && pos == 0);
        }
    }
}

// Feeds `stream` in the given chunk sizes (the last chunk takes what is left).
static Parts parse(const Bytes &stream, const std::vector<size_t> &cuts, size_t &pending) {
    UmpReader r;
    Parts out;
    size_t at = 0;
    auto drain = [&] {
        uint32_t type;
        std::span<const uint8_t> payload;
        while (r.next(type, payload)) out.emplace_back(type, Bytes(payload.begin(), payload.end()));
    };
    for (size_t cut : cuts) {
        r.feed(std::span(stream).subspan(at, cut - at));
        at = cut;
        drain();
    }
    r.feed(std::span(stream).subspan(at));
    drain();
    pending = r.pending();
    return out;
}

static void reader() {
    // UmpParserTests.cs: two parts, then an empty stream
    Bytes s1{20, 3, 1, 2, 3, 21, 2, 0xAA, 0xBB};
    size_t pending;
    Parts p = parse(s1, {}, pending);
    CHECK(p == (Parts{{20, {1, 2, 3}}, {21, {0xAA, 0xBB}}}));
    CHECK_EQ(pending, size_t(0));
    CHECK(parse({}, {}, pending).empty());

    // a synthetic SABR response: small parts, an empty part, a 300-byte part (2-byte size),
    // a 20 000-byte one (3-byte size), a type that needs 2 bytes
    Parts want = {{ump::MEDIA_HEADER, {8, 1, 0x18, 251}},
                  {ump::MEDIA, {}},
                  {ump::MEDIA, Bytes(300, 0x5A)},
                  {ump::MEDIA_END, {1}},
                  {ump::NEXT_REQUEST_POLICY, Bytes(20000, 0x33)},
                  {200, {9, 9}}};
    for (size_t i = 0; i < want[4].second.size(); i++) want[4].second[i] = uint8_t(i * 7);
    Bytes stream;
    for (auto &[t, b] : want) ump_write_part(stream, t, b);
    CHECK(parse(stream, {}, pending) == want);
    // split in two at every byte position outside the big part's body (every 97th inside it)
    bool all = true;
    for (size_t cut = 0; cut <= stream.size(); cut++) {
        if (cut > 400 && cut + 400 < stream.size() && cut % 97) continue;
        Parts got = parse(stream, {cut}, pending);
        all = all && got == want && pending == 0;
    }
    CHECK(all);
    // three chunks at many positions, and one byte at a time
    all = true;
    for (size_t a = 0; a < 40; a++)
        for (size_t b = a; b < stream.size(); b += 997) all = all && parse(stream, {a, b}, pending) == want;
    CHECK(all);
    std::vector<size_t> bytewise;
    for (size_t i = 1; i < stream.size(); i++) bytewise.push_back(i);
    CHECK(parse(stream, bytewise, pending) == want);

    // truncated: every prefix gives the whole parts before the cut and holds the rest
    all = true;
    for (size_t n = 0; n < stream.size(); n++) {
        if (n > 400 && n + 400 < stream.size() && n % 97) continue;
        Bytes cut(stream.begin(), stream.begin() + long(n));
        Parts got = parse(cut, {n / 2}, pending);
        Bytes whole;
        for (size_t i = 0; i < got.size(); i++) {
            all = all && got[i] == want[i];
            ump_write_part(whole, got[i].first, got[i].second);
        }
        all = all && got.size() < want.size() && whole.size() + pending == n;
    }
    CHECK(all);

    // a size of 4 GB never makes it allocate or read: the part just never completes
    Bytes huge{21, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3};
    CHECK(parse(huge, {}, pending).empty());
    CHECK_EQ(pending, huge.size());
}

int main() {
    varints();
    reader();
    return check_done();
}
