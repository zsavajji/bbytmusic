// The segment cache and CachedSource, over a fake inner Source in a temp folder.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <vector>

#include "check.h"
#include "stream/cached_source.h"
#include "stream/segment_cache.h"
#include "stream/webm_fixture.h"

using namespace bbm;

namespace {

// One synthetic track and what was asked of it.
struct Fake {
    webm_fixture::File file = webm_fixture::make({0, 10000, 20000, 30000}, {1000, 900, 1100, 700}, 38000.0);
    int made = 0, opens = 0, cancels = 0, fail_at = -1;
    int stale_at = -1;   // this segment fails on every Source made before `stale_until` (an expired url)
    int stale_until = 0;
    std::vector<int> reads;
    int64_t length() const { return int64_t(file.whole().size()); }
};

class FakeSource : public Source {
public:
    explicit FakeSource(Fake &f) : f_(f), born_(f.made) {}
    bool open(std::string &err) override {
        f_.opens++;
        init_ = f_.file.head;
        return parse_webm_index(init_, f_.length(), 0, idx_, err);
    }
    const std::vector<uint8_t> &init() const override { return init_; }
    int segment_count() const override { return int(idx_.segments.size()); }
    int64_t duration_ms() const override { return idx_.duration_ms; }
    int segment_at(int64_t ms) const override { return idx_.segment_at(ms); }
    bool read(int i, Segment &out, std::string &err) override {
        f_.reads.push_back(i);
        if (i == f_.fail_at) { err = "read failed (test)"; return false; }
        if (i == f_.stale_at && born_ < f_.stale_until) { err = "HTTP 403 (test)"; return false; }
        out.index = i;
        out.start_ms = idx_.segments[size_t(i)].start_ms;
        out.duration_ms = idx_.segments[size_t(i)].duration_ms;
        out.bytes = f_.file.clusters[size_t(i)];
        return true;
    }
    void cancel() override { f_.cancels++; }

private:
    Fake &f_;
    int born_;   // f.made when this Source was made
    std::vector<uint8_t> init_;
    WebmIndex idx_;
};

std::unique_ptr<CachedSource> source(SegmentCache &c, Fake &f, const std::string &id) {
    return std::make_unique<CachedSource>(c, id, [&f](StreamInfo &info, std::string &) -> std::unique_ptr<Source> {
        f.made++;
        info.itag = 251;
        info.content_length = f.length();
        info.last_modified = 1700000000;
        return std::make_unique<FakeSource>(f);
    });
}

// Opens and reads the given segments; true if every byte is what the fake file holds.
bool play(SegmentCache &c, Fake &f, const std::string &id, std::vector<int> which) {
    auto s = source(c, f, id);
    std::string err;
    if (!s->open(err) || s->init() != f.file.head || s->segment_count() != 4 || s->segment_at(25000) != 2) return false;
    bool ok = true;
    for (int i : which) {
        Segment seg;
        if (!s->read(i, seg, err)) { ok = false; continue; }
        ok = ok && seg.bytes == f.file.clusters[size_t(i)] && seg.index == i && seg.start_ms == 10000 * i;
    }
    return ok;
}

int64_t disk_bytes(const std::string &dir) {
    int64_t n = 0;
    if (DIR *d = ::opendir(dir.c_str())) {
        while (dirent *e = ::readdir(d)) {
            const std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            struct stat st;
            const std::string p = dir + "/" + name;
            if (::stat(p.c_str(), &st) != 0) continue;
            n += S_ISDIR(st.st_mode) ? disk_bytes(p) : int64_t(st.st_size);
        }
        ::closedir(d);
    }
    return n;
}

std::string temp_dir() {
    char t[] = "/tmp/bbm_cache_XXXXXX";
    return mkdtemp(t);
}

void truncate_file(const std::string &p, size_t keep) {
    FILE *f = std::fopen(p.c_str(), "r+b");
    if (f) { (void)::ftruncate(fileno(f), off_t(keep)); std::fclose(f); }
}

// A smaller cap evicts down to it at once; clear() drops every track but the open one.
void resize_and_clear() {
    const std::string dir = temp_dir();
    Fake f;
    SegmentCache c(dir, int64_t(1) << 30);
    auto a = source(c, f, "trackA");   // stays open, and is the least recently used
    std::string err;
    CHECK(a->open(err));
    for (int i = 0; i < 4; i++) { Segment s; CHECK(a->read(i, s, err)); }
    CHECK(play(c, f, "trackB", {0, 1, 2, 3}));
    CHECK(play(c, f, "trackC", {0, 1, 2, 3}));
    const int64_t one = c.bytes() / 3;   // the fake tracks are all the same size

    c.set_cap(one * 5 / 2);
    CHECK_EQ(c.cap(), one * 5 / 2);
    CHECK(c.bytes() <= c.cap());
    CHECK(c.has("trackA.251"));
    CHECK(!c.has("trackB.251"));
    CHECK(c.has("trackC.251"));

    c.clear();
    CHECK(c.has("trackA.251"));
    CHECK(!c.has("trackC.251"));
    CHECK_EQ(c.bytes(), one);
    CHECK_EQ(c.bytes(), disk_bytes(dir));

    a.reset();
    c.clear();
    CHECK_EQ(c.bytes(), int64_t(0));
    CHECK_EQ(disk_bytes(dir), int64_t(0));
}

}  // namespace

int main() {
    const int64_t big = 1 << 30;
    resize_and_clear();

    // A second run makes no inner call, even from a new SegmentCache over the same folder.
    {
        const std::string dir = temp_dir();
        Fake f;
        {
            SegmentCache c(dir, big);
            CHECK(play(c, f, "vidA", {0, 1, 2, 3}));
            CHECK_EQ(f.made, 1);
            CHECK_EQ(f.reads.size(), size_t(4));
            CHECK_EQ(c.fetches.load(), 5);   // one open, four reads
            CHECK_EQ(c.bytes(), disk_bytes(dir));
        }
        f.made = f.opens = 0;
        f.reads.clear();
        SegmentCache c(dir, big);
        CHECK(c.has("vidA.251"));
        CHECK(play(c, f, "vidA", {3, 0, 2, 1}));
        CHECK_EQ(f.made, 0);
        CHECK_EQ(f.opens, 0);
        CHECK_EQ(f.reads.size(), size_t(0));
        CHECK_EQ(c.fetches.load(), 0);
    }

    // A partial cache fetches only the gaps; a failed read is not kept.
    {
        const std::string dir = temp_dir();
        SegmentCache c(dir, big);
        Fake f;
        f.fail_at = 1;
        CHECK(!play(c, f, "vidB", {0, 1, 2}));   // 1 fails
        f.fail_at = -1;
        f.made = f.opens = 0;
        f.reads.clear();
        CHECK(play(c, f, "vidB", {0, 1, 2, 3}));
        CHECK_EQ(f.made, 1);
        CHECK_EQ(f.opens, 1);
        CHECK((f.reads == std::vector<int>{1, 3}));
        f.made = 0;
        f.reads.clear();
        CHECK(play(c, f, "vidB", {0, 1, 2, 3}));
        CHECK_EQ(f.made, 0);
    }

    // A read that fails on an old stream (its url expired during a pause) is tried again on a
    // stream resolved anew, once; the track plays on and the segment is kept.
    {
        const std::string dir = temp_dir();
        SegmentCache c(dir, big);
        Fake f;
        f.stale_at = 2;
        f.stale_until = 2;   // the first Source made (made == 1) is stale for segment 2
        CHECK(play(c, f, "vidS", {0, 1, 2, 3}));
        CHECK_EQ(f.made, 2);
        CHECK((f.reads == std::vector<int>{0, 1, 2, 2, 3}));
        f.made = 0;
        f.reads.clear();
        CHECK(play(c, f, "vidS", {0, 1, 2, 3}));   // all of it from the disk now
        CHECK_EQ(f.made, 0);
        // Stale twice over: the second failure is the track's.
        Fake g;
        g.stale_at = 1;
        g.stale_until = 99;
        CHECK(!play(c, g, "vidT", {0, 1}));
        CHECK_EQ(g.made, 2);
    }

    // A truncated segment, a truncated init and a corrupt meta are noticed and fetched again.
    {
        const std::string dir = temp_dir();
        SegmentCache c(dir, big);
        Fake f;
        CHECK(play(c, f, "vidC", {0, 1, 2, 3}));
        truncate_file(dir + "/vidC.251/2", 10);
        f.made = 0;
        f.reads.clear();
        CHECK(play(c, f, "vidC", {0, 1, 2, 3}));
        CHECK((f.reads == std::vector<int>{2}));
        CHECK_EQ(c.bytes(), disk_bytes(dir));

        truncate_file(dir + "/vidC.251/init", 5);
        f.reads.clear();
        f.opens = 0;
        CHECK(play(c, f, "vidC", {0, 1, 2, 3}));
        CHECK_EQ(f.opens, 1);
        CHECK_EQ(f.reads.size(), size_t(4));   // the whole entry was dropped

        FILE *m = std::fopen((dir + "/vidC.251/meta").c_str(), "wb");
        std::fputs("bbmusic-cache 1\nitag 251\ngarbage\n", m);
        std::fclose(m);
        f.reads.clear();
        f.opens = 0;
        CHECK(play(c, f, "vidC", {0, 1, 2, 3}));
        CHECK_EQ(f.opens, 1);
        CHECK_EQ(f.reads.size(), size_t(4));
        CHECK_EQ(c.bytes(), disk_bytes(dir));

        // a meta whose table disagrees with the content length
        m = std::fopen((dir + "/vidC.251/meta").c_str(), "r+b");
        std::fseek(m, 0, SEEK_END);
        std::fputs("1 1 1\n", m);   // an extra line: rejected
        std::fclose(m);
        f.opens = 0;
        CHECK(play(c, f, "vidC", {0}));
        CHECK_EQ(f.opens, 1);
    }

    // Eviction keeps the newest and the open one; the cap holds throughout.
    {
        const std::string dir = temp_dir();
        Fake f;
        int64_t one = 0;
        {
            SegmentCache probe(temp_dir(), big);
            CHECK(play(probe, f, "size", {0, 1, 2, 3}));
            one = probe.bytes();   // one whole track on disk
        }
        const int64_t cap = one * 7 / 2;
        SegmentCache c(dir, cap);
        auto a = source(c, f, "trackA");   // stays open throughout, and is the least recently used
        std::string err;
        CHECK(a->open(err));
        for (int i = 0; i < 4; i++) { Segment s; CHECK(a->read(i, s, err)); }
        CHECK(play(c, f, "trackB", {0, 1, 2, 3}));
        CHECK(play(c, f, "trackC", {0, 1, 2, 3}));
        CHECK(c.bytes() <= cap);
        CHECK(play(c, f, "trackD", {0, 1, 2, 3}));
        CHECK(c.bytes() <= cap);
        CHECK(c.has("trackA.251"));
        CHECK(!c.has("trackB.251"));
        CHECK(c.has("trackC.251"));
        CHECK(c.has("trackD.251"));
        CHECK_EQ(c.bytes(), disk_bytes(dir));
        a.reset();

        // The order of use survives a restart (the meta's mtime), and a smaller cap evicts the oldest.
        sleep(1);
        CHECK(play(c, f, "trackC", {0}));   // C is now newer than D on disk
        SegmentCache again(dir, one * 3 / 2);
        CHECK(again.bytes() <= one * 3 / 2);
        CHECK(again.has("trackC.251"));
        CHECK(!again.has("trackD.251"));
        CHECK(!again.has("trackA.251"));
        CHECK_EQ(again.bytes(), disk_bytes(dir));
    }

    // One open track bigger than the cap: its segments are dropped, the cap holds, it still plays.
    {
        const std::string dir = temp_dir();
        Fake f;
        SegmentCache c(dir, 1500);
        CHECK(play(c, f, "huge", {0, 1, 2, 3}));
        CHECK(c.bytes() <= 1500);
        CHECK_EQ(c.bytes(), disk_bytes(dir));
    }

    // cancel() passes through; an id that is not a file name is played without the cache.
    {
        const std::string dir = temp_dir();
        SegmentCache c(dir, big);
        Fake f;
        auto s = source(c, f, "vidE");
        std::string err;
        s->cancel();   // nothing to pass it to yet
        CHECK_EQ(f.cancels, 0);
        CHECK(s->open(err));
        s->cancel();
        CHECK_EQ(f.cancels, 1);
        CHECK(play(c, f, "../evil", {0, 1}));
        CHECK(!c.has("../evil.251"));
        CHECK_EQ(disk_bytes(dir), c.bytes());
    }
    return check_done();
}
