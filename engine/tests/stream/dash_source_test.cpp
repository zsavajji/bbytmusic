// DashSource and make_source against a fake server holding a synthetic WebM file.
#include "check.h"
#include "stream/dash_source.h"
#include "stream/sabr_source.h"
#include "stream/webm_fixture.h"

#include <chrono>
#include <string>
#include <thread>

using namespace bbm;
using webm_fixture::Bytes;

struct FakeDash {
    webm_fixture::File file = webm_fixture::make({0, 10000, 20000}, {2000, 1500, 400}, 25000.0);
    Bytes whole = file.whole();
    std::vector<std::string> ranges;
    long status = 206;
    bool ignore_range = false, block = false;

    StreamInfo info() const {
        StreamInfo si;
        si.itag = 251;
        si.url = "https://example.invalid/vp?itag=251";
        si.user_agent = "test-agent";
        si.content_length = int64_t(whole.size());
        si.index_end = int64_t(file.head.size()) - 1;
        si.duration_ms = 24000;
        return si;
    }
    HttpResponse serve(const HttpRequest &req) {
        HttpResponse r;
        std::string range;
        for (auto &[k, v] : req.headers)
            if (k == "Range") range = v;
        ranges.push_back(range);
        if (block) {
            while (!req.cancel->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            r.error = "cancelled";
            return r;
        }
        r.status = status;
        size_t first = std::stoul(range.substr(6)), last = std::stoul(range.substr(range.find('-') + 1));
        if (ignore_range) first = 0, last = whole.size() - 1;
        last = std::min(last, whole.size() - 1);
        r.body.assign(whole.begin() + long(first), whole.begin() + long(last) + 1);
        return r;
    }
    HttpSend sender() {
        return [this](const HttpRequest &r) { return serve(r); };
    }
};

int main() {
    FakeDash fake;
    std::string err;
    {
        DashSource src(fake.info(), fake.sender());
        CHECK(src.open(err));
        CHECK_EQ(fake.ranges[0], "bytes=0-" + std::to_string(fake.file.head.size() - 1));
        CHECK(src.init() == fake.file.head);
        CHECK_EQ(src.segment_count(), 3);
        CHECK_EQ(src.duration_ms(), 25000);
        CHECK_EQ(src.segment_at(10000), 1);
        Bytes whole = src.init();
        for (int i : {2, 0, 1}) {  // any order
            Segment seg;
            CHECK(src.read(i, seg, err));
            CHECK_EQ(seg.index, i);
            CHECK(seg.bytes == fake.file.clusters[size_t(i)]);
        }
        Segment seg;
        for (int i = 0; i < 3; i++) {
            CHECK(src.read(i, seg, err));
            whole.insert(whole.end(), seg.bytes.begin(), seg.bytes.end());
        }
        CHECK(whole == fake.whole);
        CHECK_EQ(seg.start_ms, 20000);
        CHECK_EQ(seg.duration_ms, 5000);
        CHECK(!src.read(3, seg, err));
    }
    {
        FakeDash bad;
        bad.status = 403;
        DashSource src(bad.info(), bad.sender());
        CHECK(!src.open(err));
        CHECK(err.find("http 403") != std::string::npos);
    }
    {
        FakeDash bad;  // a 200 with the whole file instead of the range
        bad.status = 200;
        bad.ignore_range = true;
        DashSource src(bad.info(), bad.sender());
        CHECK(!src.open(err));
        CHECK(err.find("bytes") != std::string::npos);
    }
    {
        FakeDash f;  // cancel unblocks a read, and the next read runs normally
        DashSource src(f.info(), f.sender());
        CHECK(src.open(err));
        f.block = true;
        std::thread t([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            src.cancel();
        });
        Segment seg;
        CHECK(!src.read(0, seg, err));
        t.join();
        CHECK(err.find("cancelled") != std::string::npos);
        f.block = false;
        CHECK(src.read(0, seg, err));
    }
    {
        StreamInfo si = fake.info();
        si.url.clear();
        DashSource src(si, fake.sender());
        CHECK(!src.open(err));
    }
    // make_source: DASH by default, SABR when asked for or when there is no url
    StreamInfo both = fake.info();
    both.sabr_url = "https://example.invalid/sabr";
    both.ustreamer_config = "AAEC";
    CHECK(dynamic_cast<DashSource *>(make_source(both).get()) != nullptr);
    CHECK(dynamic_cast<SabrSource *>(make_source(both, true).get()) != nullptr);
    StreamInfo sabr_only = both;
    sabr_only.url.clear();
    CHECK(dynamic_cast<SabrSource *>(make_source(sabr_only).get()) != nullptr);
    StreamInfo dash_only = fake.info();
    CHECK(dynamic_cast<DashSource *>(make_source(dash_only, true).get()) != nullptr);
    CHECK(make_source(StreamInfo{}) == nullptr);
    return check_done();
}
