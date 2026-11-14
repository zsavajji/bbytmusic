// Fixture: tests/fixtures/media/tone.webm, made with
//   ffmpeg -f lavfi -i sine=frequency=440:sample_rate=48000:duration=5 -ac 2 -c:a libopus -b:a 64k tone.webm
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.h"
#include "media/opus_decoder.h"
#include "media/webm_demux.h"

using namespace bbm;
using Dec = bbm::OpusDecoder;

static std::vector<uint8_t> read_file(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

struct Out { std::vector<int16_t> pcm; std::vector<int64_t> pts; bool ok = true; };

// Feeds `bytes` in chunks of `chunk`, draining packets after each feed.
static void run(WebmDemuxer &d, Dec &dec, const std::vector<uint8_t> &bytes, size_t from,
                size_t to, size_t chunk, Out &o) {
    std::string err;
    for (size_t i = from; i < to; i += chunk) {
        size_t n = std::min(chunk, to - i);
        if (!d.feed({bytes.data() + i, n}, err)) { o.ok = false; return; }
        Packet p;
        while (d.next(p)) {
            o.pts.push_back(p.pts_ms);
            if (!dec.decode(p, o.pcm, err)) { o.ok = false; return; }
        }
    }
}

static Out decode_all(const std::vector<uint8_t> &b, size_t chunk) {
    WebmDemuxer d;
    Dec dec;
    std::string err;
    Out o;
    // the header arrives with the first bytes; decode needs channels, so init lazily
    size_t head = 0;
    while (head < b.size() && !d.has_header()) {
        d.feed({b.data() + head, 1}, err);
        Packet p; d.next(p);
        head++;
    }
    d.reset();   // keeps the track info
    CHECK(d.has_header());
    CHECK(dec.init(d.channels(), err));
    dec.set_skip(d.pre_skip());
    run(d, dec, b, 0, b.size(), chunk, o);
    return o;
}

static std::vector<size_t> cluster_offsets(const std::vector<uint8_t> &b) {
    std::vector<size_t> r;
    for (size_t i = 0; i + 8 < b.size(); i++)
        if (b[i] == 0x1F && b[i + 1] == 0x43 && b[i + 2] == 0xB6 && b[i + 3] == 0x75) r.push_back(i);
    return r;
}

// Cluster time in ms from the Timecode (E7) element right after the cluster header.
static int64_t cluster_time(const std::vector<uint8_t> &b, size_t off) {
    size_t i = off + 4;
    int n = 1;
    for (uint8_t m = 0x80; !(b[i] & m); m >>= 1) n++;
    i += n;
    if (b[i] != 0xE7) return -1;
    size_t len = b[i + 1] & 0x7f;
    int64_t v = 0;
    for (size_t k = 0; k < len; k++) v = v << 8 | b[i + 2 + k];
    return v;
}

int main() {
    const auto bytes = read_file(std::string(BBM_FIXTURES) + "/media/tone.webm");
    CHECK(bytes.size() > 1000);

    // 1. whole file at once
    Out whole = decode_all(bytes, bytes.size());
    CHECK(whole.ok);
    const size_t frames = whole.pcm.size() / 2;
    std::printf("frames after pre-skip: %zu, packets: %zu\n", frames, whole.pts.size());
    CHECK(frames + 960 >= 240000 && frames <= 240000 + 960);   // within a packet of 5 s
    int64_t energy = 0;
    int pairs = 0;
    for (size_t i = 1; i < frames; i++) {
        energy += std::abs(whole.pcm[2 * i]);
        pairs += whole.pcm[2 * (i - 1)] < 0 && whole.pcm[2 * i] >= 0;
    }
    std::printf("zero-crossing pairs: %d (%.1f /s)\n", pairs, pairs * 48000.0 / frames);
    CHECK(energy / int64_t(frames) > 1000);
    CHECK(pairs > 430 * 5 && pairs < 450 * 5);
    for (size_t i = 1; i < whole.pts.size(); i++) CHECK(whole.pts[i] >= whole.pts[i - 1]);

    // 2. any chunking gives the same PCM
    for (size_t chunk : {size_t(1), size_t(7), size_t(1000), size_t(65536)}) {
        Out o = decode_all(bytes, chunk);
        CHECK(o.ok);
        CHECK(o.pcm == whole.pcm);
        CHECK(o.pts == whole.pts);
    }

    // 3. head, reset, then bytes from a later cluster
    auto cl = cluster_offsets(bytes);
    std::printf("clusters: %zu\n", cl.size());
    CHECK(cl.size() >= 2);
    if (cl.size() >= 2) {
        WebmDemuxer d;
        std::string err;
        CHECK(d.feed({bytes.data(), cl[0]}, err));
        CHECK(d.has_header());
        d.reset();
        CHECK(d.has_header());
        CHECK_EQ(d.channels(), 2);
        CHECK(d.pre_skip() > 0);
        CHECK(d.feed({bytes.data() + cl[1], bytes.size() - cl[1]}, err));
        Packet p;
        CHECK(d.next(p));
        std::printf("later cluster at %lld ms, first pts %lld\n", (long long)cluster_time(bytes, cl[1]), (long long)p.pts_ms);
        CHECK_EQ(p.pts_ms, cluster_time(bytes, cl[1]));
        CHECK(p.size > 0);
    }

    // 4. truncations never crash and never invent packets
    for (size_t len = 0; len < bytes.size(); len += (len < 600 ? 1 : 3001)) {
        WebmDemuxer d;
        Dec dec;
        std::string err;
        CHECK(dec.init(2, err));
        Out o;
        run(d, dec, bytes, 0, len, 333, o);
        CHECK(o.ok);
        CHECK(o.pts.size() <= whole.pts.size());
        for (size_t i = 0; i < o.pts.size(); i++) CHECK_EQ(o.pts[i], whole.pts[i]);
        if (!cl.empty() && len < cl[0]) CHECK(o.pts.empty());
    }

    // 5. random bytes, and bytes with one corrupted byte at a time
    uint32_t s = 12345;
    auto rnd = [&] { s = s * 1664525u + 1013904223u; return uint8_t(s >> 24); };
    for (int t = 0; t < 50; t++) {
        std::vector<uint8_t> r(4096);
        for (auto &c : r) c = rnd();
        WebmDemuxer d;
        std::string err;
        Packet p;
        d.feed(r, err);
        CHECK(!d.next(p));
        CHECK(!d.has_header());
    }
    for (size_t at = 0; at < 3000; at += 7) {
        auto c = bytes;
        c[at] = rnd();
        WebmDemuxer d;
        Dec dec;
        std::string err;
        dec.init(2, err);
        Out o;
        run(d, dec, c, 0, c.size(), 4096, o);   // result unconstrained; ASan watches
    }
    {   // handcrafted: huge sizes, a block with a bad track, a short block
    const uint8_t evil[] = {0x1F,0x43,0xB6,0x75, 0xFF,            // Cluster, unknown size
        0xA3, 0x84, 0x81,0x00,0x00,0x00,                          // SimpleBlock with no frame
        0xA3, 0x85, 0x82,0x00,0x00,0x00,0x01,                     // wrong track
        0x63,0xA2, 0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,       // CodecPrivate of 2^56 bytes
        0xA3, 0x84, 0x81};                                        // truncated
        WebmDemuxer d;
        std::string err;
        Packet p;
        d.feed(evil, err);
        CHECK(!d.next(p));
        CHECK(!d.feed(evil, err));   // sticky error
        d.reset();
        CHECK(d.feed(evil, err));
    }
    return check_done();
}
