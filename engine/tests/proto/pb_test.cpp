#include "check.h"
#include "proto/pb.h"

#include <string>
#include <vector>

using namespace bbm;
using Bytes = std::vector<uint8_t>;

static Bytes varint_bytes(uint64_t v) {
    Bytes b;
    PbWriter::put_varint(b, v);
    return b;
}

// All fields of a message, or nothing if it is malformed.
static std::vector<PbField> fields(const Bytes &b, bool &ok) {
    std::vector<PbField> out;
    PbReader r(b);
    PbField f;
    while (r.next(f)) out.push_back(f);
    ok = !r.failed() && r.done();
    return out;
}

static void writer_vectors() {  // YTMusicWP's MiniProtoWriterTests
    CHECK(varint_bytes(0) == Bytes{0x00});
    CHECK(varint_bytes(1) == Bytes{0x01});
    CHECK(varint_bytes(127) == Bytes{0x7F});
    CHECK(varint_bytes(128) == (Bytes{0x80, 0x01}));
    CHECK(varint_bytes(300) == (Bytes{0xAC, 0x02}));
    CHECK(varint_bytes(UINT64_MAX).size() == 10);
    CHECK(PbWriter().varint(1, 0).data() == (Bytes{0x08, 0x00}));
    CHECK(PbWriter().varint(1, 150).data() == (Bytes{0x08, 0x96, 0x01}));
    CHECK(PbWriter().string(2, "hello").data() == (Bytes{0x12, 0x05, 'h', 'e', 'l', 'l', 'o'}));
    Bytes raw{0xDE, 0xAD, 0xBE, 0xEF};
    CHECK(PbWriter().bytes(3, raw).data() == (Bytes{0x1A, 0x04, 0xDE, 0xAD, 0xBE, 0xEF}));
    // fetch_probe.py's f_float(35, 1.0): key 0x1D (35 << 3 | 5 = 285 -> 0x9D 0x02), 00 00 80 3F
    CHECK(PbWriter().float32(35, 1.0f).data() == (Bytes{0x9D, 0x02, 0x00, 0x00, 0x80, 0x3F}));
    // a nested message: field 1 { field 16: 101, field 17: "1.02" }
    PbWriter client;
    client.varint(16, 101).string(17, "1.02");
    Bytes nested = PbWriter().message(1, client).data();
    CHECK(nested == (Bytes{0x0A, 0x0A, 0x80, 0x01, 0x65, 0x8A, 0x01, 0x04, '1', '.', '0', '2'}));
}

static void round_trip() {
    PbWriter inner;
    inner.varint(1, 251).varint(2, 1766955883819090ull);
    PbWriter w;
    w.varint(3, 649264)
        .varint(4, 1710000000000ull)
        .varint(5, UINT64_MAX)
        .fixed32(6, 0xDEADBEEF)
        .float32(7, 1.5f)
        .string(8, "")
        .message(9, inner)
        .varint(536870911, 1);  // the largest field number
    bool ok;
    auto f = fields(w.data(), ok);
    CHECK(ok);
    CHECK_EQ(f.size(), size_t(8));
    if (f.size() != 8) return;
    CHECK_EQ(f[0].number, 3u);
    CHECK_EQ(f[0].value, 649264u);
    CHECK_EQ(f[1].value, 1710000000000ull);
    CHECK_EQ(f[2].value, UINT64_MAX);
    CHECK(f[3].wire == PbWire::Fixed32);
    CHECK_EQ(f[3].value, 0xDEADBEEFu);
    CHECK_EQ(f[4].f32(), 1.5f);
    CHECK(f[5].wire == PbWire::Bytes);
    CHECK_EQ(f[5].bytes.size(), size_t(0));
    CHECK_EQ(f[7].number, 536870911u);
    // the nested message reads back from the span
    Bytes sub(f[6].bytes.begin(), f[6].bytes.end());
    auto g = fields(sub, ok);
    CHECK(ok);
    CHECK_EQ(g.size(), size_t(2));
    if (g.size() == 2) {
        CHECK_EQ(g[0].value, 251u);
        CHECK_EQ(g[1].value, 1766955883819090ull);
    }
    // fixed64, written by hand: field 2, wire 1
    Bytes f64{0x11, 1, 2, 3, 4, 5, 6, 7, 8};
    auto h = fields(f64, ok);
    CHECK(ok);
    CHECK(h.size() == 1 && h[0].wire == PbWire::Fixed64 && h[0].value == 0x0807060504030201ull);
}

static void hostile() {
    Bytes good = PbWriter().varint(1, 300).string(2, "abc").fixed32(3, 7).data();
    // every truncation of a valid message fails cleanly (or ends at a field boundary)
    for (size_t n = 0; n < good.size(); n++) {
        Bytes cut(good.begin(), good.begin() + long(n));
        bool ok;
        auto f = fields(cut, ok);
        bool boundary = n == 0 || n == 3 || n == 8;  // after 08 AC 02, after 12 03 'abc'
        CHECK_EQ(ok, boundary);
    }
    bool ok;
    fields({0x00, 0x01}, ok);  // field number 0
    CHECK(!ok);
    fields({0x0B}, ok);  // wire type 3 (start group)
    CHECK(!ok);
    fields({0x0E}, ok);  // wire type 6
    CHECK(!ok);
    fields({0x0A, 0x05, 'a'}, ok);  // length past the end
    CHECK(!ok);
    fields({0x0A, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F}, ok);  // a 4 GB length
    CHECK(!ok);
    fields({0x0A, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01}, ok);  // UINT64_MAX length
    CHECK(!ok);
    fields({0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02}, ok);  // 65-bit varint
    CHECK(!ok);
    fields({0x08, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00}, ok);  // 11 bytes
    CHECK(!ok);
    fields({0x80, 0x80, 0x80, 0x80, 0x80, 0x01, 0x00}, ok);  // field number past 2^29
    CHECK(!ok);
    fields({0x15, 1, 2, 3}, ok);  // fixed32 cut short
    CHECK(!ok);
    // after a failure the reader stays failed
    Bytes bad{0x08, 0x01, 0x0B, 0x08, 0x02};
    PbReader r(bad);
    PbField f;
    CHECK(r.next(f));
    CHECK(!r.next(f));
    CHECK(r.failed());
    CHECK(!r.next(f));
    // all 2-byte inputs: never crash, never read out of bounds (ASan watches)
    for (int a = 0; a < 256; a++)
        for (int b = 0; b < 256; b++) {
            Bytes in{uint8_t(a), uint8_t(b)};
            PbReader rr(in);
            while (rr.next(f)) {}
        }
}

static void messages() {  // the payload vectors of YTMusicWP's UmpParserTests
    Bytes cookie{0xAA, 0xBB, 0xCC};
    Bytes policy = PbWriter().varint(4, 2500).bytes(7, cookie).data();  // NextRequestPolicy
    PbMsg p(policy);
    CHECK(p.ok());
    CHECK_EQ(p.u(4), 2500u);
    CHECK(Bytes(p.bytes(7).begin(), p.bytes(7).end()) == cookie);
    CHECK(!p.has(1));
    CHECK_EQ(p.u(1, 77), 77u);
    CHECK(p.bytes(4).empty());  // a varint is not bytes
    CHECK_EQ(p.u(7, 5), 5u);    // nor bytes a varint
    Bytes seek = PbWriter().varint(1, 3249658195ull).varint(2, 1000).varint(3, 10).data();  // SabrSeek
    PbMsg s(seek);
    CHECK(s.ok() && s.u(1) == 3249658195ull && s.u(2) == 1000 && s.u(3) == 10);
    Bytes live = PbWriter().varint(3, 649264).varint(4, 1710000000000ull).varint(5, 1710000005000ull).data();
    PbMsg l(live);
    CHECK(l.u(3) == 649264 && l.u(4) == 1710000000000ull && l.u(5) == 1710000005000ull);
    // empty and malformed: defaults
    PbMsg e(Bytes{});
    CHECK(e.ok() && !e.has(4) && e.u(4) == 0 && e.bytes(7).empty());
    PbMsg bad(Bytes{0x20, 0xC4, 0x13, 0x3A, 0x09});  // field 7 claims 9 bytes, has 0
    CHECK(!bad.ok() && !bad.has(4));
    // the last value wins; strings
    Bytes twice = PbWriter().varint(1, 1).string(2, "a").varint(1, 2).string(2, "redirect").data();
    PbMsg t(twice);
    CHECK_EQ(t.u(1), 2u);
    CHECK_EQ(t.str(2), "redirect");
}

int main() {
    writer_vectors();
    round_trip();
    hostile();
    messages();
    return check_done();
}
