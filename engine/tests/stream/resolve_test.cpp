#include "check.h"
#include "stream/resolve.h"

#include <fstream>
#include <sstream>
#include <string>

using namespace bbm;

static std::string read_file(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

// A response with the given adaptiveFormats and streamingData extras.
static std::string player(const std::string &formats, const std::string &sd_extra = "",
                          const std::string &status = R"("status":"OK")") {
    return R"({"playabilityStatus":{)" + status + R"(},"streamingData":{"adaptiveFormats":[)" + formats + "]" +
           sd_extra + R"(},"videoDetails":{"videoId":"abcdefghijk"},)" +
           R"("playerConfig":{"mediaCommonConfig":{"mediaUstreamerRequestConfig":{"videoPlaybackUstreamerConfig":"AAEC"}}}})";
}
static std::string fmt(int itag, bool url = true, bool ranges = true) {
    std::string s = R"({"itag":)" + std::to_string(itag) + R"(,"mimeType":"audio/webm; codecs=\"opus\"",)" +
                    R"("lastModified":"1700000000000001","contentLength":"1000","approxDurationMs":"5000")";
    if (url) s += R"(,"url":"https://example.invalid/vp?itag=)" + std::to_string(itag) + "\"";
    if (ranges) s += R"(,"initRange":{"start":"0","end":"9"},"indexRange":{"start":"10","end":"99"})";
    return s + "}";
}
static const std::string kSabr = R"(,"serverAbrStreamingUrl":"https://example.invalid/sabr?x=1")";

static void fixture() {
    const PlayerClient &visionos = player_clients()[0];
    CHECK_EQ(std::string(visionos.name), "VISIONOS");
    std::string json = read_file(BBM_FIXTURES "/stream/player_visionos.json");
    CHECK(!json.empty());
    StreamInfo si;
    std::string err;
    CHECK(parse_player_response(json, visionos, si, err));
    CHECK_EQ(err, "");
    CHECK_EQ(si.video_id, "dQw4w9WgXcQ");
    CHECK_EQ(si.client, "VISIONOS");
    CHECK_EQ(si.itag, 251);
    CHECK_EQ(si.mime, "audio/webm; codecs=\"opus\"");
    CHECK_EQ(si.content_length, 3433755);
    CHECK_EQ(si.duration_ms, 213061);
    CHECK_EQ(si.last_modified, 1766955883819090);
    CHECK_EQ(si.init_end, 258);
    CHECK_EQ(si.index_start, 259);
    CHECK_EQ(si.index_end, 629);
    CHECK_EQ(si.url, "https://example.invalid/videoplayback?itag=251");
    CHECK_EQ(si.sabr_url, "https://example.invalid/videoplayback?sabr=1&expire=0");
    CHECK_EQ(si.ustreamer_config, "CgQIARAC");
    CHECK_EQ(si.user_agent, std::string(visionos.user_agent));
    CHECK_EQ(si.client_version, "1.02");
    CHECK_EQ(si.client_id, 101);
    CHECK(si.loudness_db > 0.97 && si.loudness_db < 0.99);
}

static void choices() {
    const PlayerClient &ios = *find_player_client("IOS");
    CHECK_EQ(ios.id, 5);
    CHECK(find_player_client("WEB") == nullptr);
    StreamInfo si;
    std::string err;
    // Opus order, wherever the formats stand in the list
    CHECK(parse_player_response(player(fmt(140) + "," + fmt(249) + "," + fmt(250)), ios, si, err));
    CHECK_EQ(si.itag, 250);
    CHECK(parse_player_response(player(fmt(140) + "," + fmt(249)), ios, si, err));
    CHECK_EQ(si.itag, 249);
    CHECK(parse_player_response(player(fmt(139) + "," + fmt(140)), ios, si, err));
    CHECK_EQ(si.itag, 140);
    CHECK(!parse_player_response(player(fmt(139)), ios, si, err));
    CHECK(err.find("no Opus") != std::string::npos);
    // SABR only: no url on the format
    CHECK(parse_player_response(player(fmt(251, false), kSabr), ios, si, err));
    CHECK(si.url.empty());
    CHECK_EQ(si.sabr_url, "https://example.invalid/sabr?x=1");
    CHECK_EQ(si.ustreamer_config, "AAEC");
    CHECK_EQ(si.client, "IOS");
    CHECK_EQ(si.client_id, 5);
    CHECK_EQ(si.loudness_db, 0.0);
    // a url without ranges is no use for DASH; with SABR beside it, SABR
    CHECK(parse_player_response(player(fmt(251, true, false), kSabr), ios, si, err));
    CHECK(si.url.empty() && !si.sabr_url.empty());
    // neither
    CHECK(!parse_player_response(player(fmt(251, false)), ios, si, err));
    CHECK(err.find("neither") != std::string::npos);
    // a SABR url without a ustreamer config is no SABR
    std::string no_cfg = player(fmt(251, false), kSabr);
    no_cfg.replace(no_cfg.find("AAEC"), 4, "");
    CHECK(!parse_player_response(no_cfg, ios, si, err));
}

static void refusals() {
    const PlayerClient &c = player_clients()[0];
    StreamInfo si;
    si.itag = 7;
    std::string err;
    CHECK(!parse_player_response(
        player(fmt(251), "", R"("status":"UNPLAYABLE","reason":"The page needs to be reloaded.")"), c, si, err));
    CHECK_EQ(err, "VISIONOS: UNPLAYABLE: The page needs to be reloaded.");
    CHECK_EQ(si.itag, 7);  // untouched on failure
    CHECK(!parse_player_response(R"({"responseContext":{}})", c, si, err));
    CHECK_EQ(err, "VISIONOS: no playabilityStatus");
    CHECK(!parse_player_response("{not json", c, si, err));
    CHECK(err.find("bad JSON") != std::string::npos);
    CHECK(!parse_player_response("", c, si, err));
    // wrong types everywhere: no crash, just no format
    CHECK(!parse_player_response(R"({"playabilityStatus":{"status":"OK"},"streamingData":{"adaptiveFormats":{"itag":251}}})",
                                 c, si, err));
}

int main() {
    fixture();
    choices();
    refusals();
    return check_done();
}
