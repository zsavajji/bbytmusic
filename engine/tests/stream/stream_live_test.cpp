// Gate 0.2 as a test: one track through DASH and through SABR, byte for byte the same file.
// Needs the network. Keeps every byte in memory: never write media or urls to the repo.
#include "check.h"
#include "stream/dash_source.h"
#include "stream/resolve.h"
#include "stream/sabr_source.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace bbm;
using Bytes = std::vector<uint8_t>;

static double since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

static bool read_all(Source &src, std::vector<Bytes> &out, std::string &err) {
    for (int i = 0; i < src.segment_count(); i++) {
        Segment seg;
        if (!src.read(i, seg, err)) {
            err = "segment " + std::to_string(i) + ": " + err;
            return false;
        }
        if (seg.index != i) return false;
        out.push_back(std::move(seg.bytes));
    }
    return true;
}

int main() {
    const std::string video = "dQw4w9WgXcQ";
    const int64_t kSeekMs = 120000;
    std::string err;
    StreamInfo info;
    auto t = std::chrono::steady_clock::now();
    bool ok = resolve(video, info, err);
    CHECK(ok);
    if (!ok) {
        std::fprintf(stderr, "resolve: %s\n", err.c_str());
        return check_done();
    }
    std::printf("resolve: %.2f s, client %s, itag %d, %s, %lld bytes, %lld ms, url %s, sabr %s\n", since(t),
                info.client.c_str(), info.itag, info.mime.c_str(), (long long)info.content_length,
                (long long)info.duration_ms, info.url.empty() ? "NO" : "yes", info.sabr_url.empty() ? "NO" : "yes");
    CHECK(!info.url.empty() && !info.sabr_url.empty());
    CHECK(dynamic_cast<DashSource *>(make_source(info).get()) != nullptr);
    CHECK(dynamic_cast<SabrSource *>(make_source(info, true).get()) != nullptr);
    if (info.url.empty() || info.sabr_url.empty()) return check_done();

    DashSource dash(info);
    SabrSource sabr(info);
    t = std::chrono::steady_clock::now();
    ok = dash.open(err);
    CHECK(ok);
    if (!ok) std::fprintf(stderr, "dash open: %s\n", err.c_str());
    std::printf("dash open: %.2f s\n", since(t));
    t = std::chrono::steady_clock::now();
    ok = sabr.open(err);
    CHECK(ok);
    if (!ok) std::fprintf(stderr, "sabr open: %s\n", err.c_str());
    std::printf("sabr open: %.2f s, %d requests\n", since(t), sabr.requests());
    CHECK(dash.init() == sabr.init());
    CHECK_EQ(dash.segment_count(), sabr.segment_count());
    CHECK_EQ(dash.duration_ms(), sabr.duration_ms());
    std::printf("init %zu bytes, %d segments, %lld ms\n", dash.init().size(), dash.segment_count(),
                (long long)dash.duration_ms());
    if (dash.segment_count() < 2 || dash.segment_count() != sabr.segment_count()) return check_done();

    std::vector<Bytes> d, s;
    t = std::chrono::steady_clock::now();
    ok = read_all(dash, d, err);
    CHECK(ok);
    if (!ok) std::fprintf(stderr, "dash: %s\n", err.c_str());
    std::printf("dash read: %.2f s, %zu segments\n", since(t), d.size());
    t = std::chrono::steady_clock::now();
    int before = sabr.requests();
    ok = read_all(sabr, s, err);
    CHECK(ok);
    if (!ok) std::fprintf(stderr, "sabr: %s\n", err.c_str());
    std::printf("sabr read: %.2f s, %zu segments, %d requests\n", since(t), s.size(), sabr.requests() - before);
    CHECK(d == s);
    int64_t total = int64_t(dash.init().size());
    for (const Bytes &b : d) total += int64_t(b.size());
    CHECK_EQ(total, info.content_length);
    std::printf("init + segments: %lld bytes, content length %lld\n", (long long)total, (long long)info.content_length);

    // a seek first: fresh sources, segment_at(2:00) before anything else, then the one after it
    DashSource dash2(info);
    SabrSource sabr2(info);
    CHECK(dash2.open(err) && sabr2.open(err));
    int at = dash2.segment_at(kSeekMs);
    CHECK_EQ(at, sabr2.segment_at(kSeekMs));
    CHECK(at > 0 && at + 1 < int(d.size()));
    if (at <= 0 || at + 1 >= int(d.size())) return check_done();
    t = std::chrono::steady_clock::now();
    before = sabr2.requests();
    for (int i : {at, at + 1}) {
        Segment a, b;
        CHECK(dash2.read(i, a, err));
        bool got = sabr2.read(i, b, err);
        CHECK(got);
        if (!got) std::fprintf(stderr, "sabr seek, segment %d: %s\n", i, err.c_str());
        CHECK(a.bytes == d[size_t(i)]);
        CHECK(b.bytes == d[size_t(i)]);
        CHECK_EQ(a.start_ms, b.start_ms);
        if (i == at) {
            CHECK(a.start_ms <= kSeekMs && kSeekMs < a.start_ms + a.duration_ms);
            std::printf("seek to %lld ms: segment %d, %lld..%lld ms\n", (long long)kSeekMs, i, (long long)a.start_ms,
                        (long long)(a.start_ms + a.duration_ms));
        }
    }
    std::printf("seek reads: %.2f s, sabr %d requests\n", since(t), sabr2.requests() - before);

    // the fallbacks, SABR only: the same bytes through IOS and ANDROID
    for (const char *name : {"IOS", "ANDROID"}) {
        StreamInfo si;
        t = std::chrono::steady_clock::now();
        ok = resolve_client(video, *find_player_client(name), si, err);
        CHECK(ok);
        if (!ok) {
            std::fprintf(stderr, "%s: %s\n", name, err.c_str());
            continue;
        }
        CHECK(si.url.empty() && !si.sabr_url.empty());
        CHECK_EQ(si.itag, info.itag);
        SabrSource src(si);
        ok = src.open(err);
        CHECK(ok);
        if (!ok) std::fprintf(stderr, "%s open: %s\n", name, err.c_str());
        CHECK(src.init() == dash.init());
        for (int i : {0, 1, at}) {
            Segment seg;
            ok = src.read(i, seg, err);
            CHECK(ok);
            if (!ok) std::fprintf(stderr, "%s segment %d: %s\n", name, i, err.c_str());
            CHECK(seg.bytes == d[size_t(i)]);
        }
        std::printf("%s: SABR only, init + segments 0, 1, %d equal to DASH's, %.2f s, %d requests\n", name, at,
                    since(t), src.requests());
    }
    return check_done();
}
