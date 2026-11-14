// SponsorBlock's URL and answer parsing, on a recorded answer; no network.
#include <fstream>
#include <sstream>

#include "check.h"
#include "engine/sponsor.h"

using namespace bbm;

static std::string seg(const char *cat, const char *action, const char *a, const char *b) {
    return std::string("{\"category\":\"") + cat + "\",\"actionType\":\"" + action + "\",\"segment\":[" + a + "," + b + "]}";
}
static std::string video(const char *id, const std::string &segs) {
    return std::string("[{\"videoID\":\"") + id + "\",\"segments\":[" + segs + "]}]";
}

int main() {
    std::ifstream f(std::string(BBM_FIXTURES) + "/sponsor/prefix_f1d9.json");
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string body = ss.str();
    CHECK(!body.empty());

    std::vector<SkipSegment> out;
    std::string err;
    CHECK(parse_segments(body, "kJQP7kiw5Fk", skip_categories(), out, err));
    CHECK_EQ(out.size(), size_t(2));
    if (out.size() == 2) {
        CHECK_EQ(out[0].category, std::string("music_offtopic"));
        CHECK_EQ(out[0].from_ms, int64_t(0));
        CHECK_EQ(out[0].to_ms, int64_t(21808));
        CHECK_EQ(out[1].from_ms, int64_t(249380));
        CHECK_EQ(out[1].to_ms, int64_t(281521));
    }
    CHECK(parse_segments(body, "kJQP7kiw5Fk", {"sponsor"}, out, err));
    CHECK(out.empty());
    CHECK(parse_segments(body, "notInTheAnswer", skip_categories(), out, err));
    CHECK(out.empty());
    CHECK(!parse_segments("{\"a\":1}", "x", skip_categories(), out, err));
    CHECK(!parse_segments("nope", "x", skip_categories(), out, err));

    // overlapping and touching ones merge, in start order; the first's category stays
    CHECK(parse_segments(video("x", seg("outro", "skip", "30", "40") + "," + seg("sponsor", "skip", "10", "20.5") + "," +
                                        seg("intro", "skip", "20", "25") + "," + seg("intro", "skip", "22", "23") + "," +
                                        seg("outro", "skip", "40", "50")),
                         "x", skip_categories(), out, err));
    CHECK_EQ(out.size(), size_t(2));
    if (out.size() == 2) {
        CHECK_EQ(out[0].category, std::string("sponsor"));
        CHECK_EQ(out[0].from_ms, int64_t(10000));
        CHECK_EQ(out[0].to_ms, int64_t(25000));
        CHECK_EQ(out[1].category, std::string("outro"));
        CHECK_EQ(out[1].from_ms, int64_t(30000));
        CHECK_EQ(out[1].to_ms, int64_t(50000));
    }
    // another actionType is ignored
    CHECK(parse_segments(video("x", seg("sponsor", "mute", "1", "2") + "," + seg("sponsor", "poi", "3", "4")), "x",
                         skip_categories(), out, err));
    CHECK(out.empty());

    CHECK_EQ(sponsor_url("kJQP7kiw5Fk", {"sponsor", "music_offtopic"}),
             std::string("https://sponsor.ajay.app/api/skipSegments/f1d9?categories=%5B%22sponsor%22,%22music_offtopic%22%5D"
                         "&actionTypes=%5B%22skip%22%5D"));
    return check_done();
}
