#include "stream/resolve.h"

#include <mutex>

#include "net/http.h"
#include "util/json.h"
#include "util/log.h"

namespace bbm {
namespace {

// The visitor data comes from a WEB_REMIX search, as tools/gates/player_matrix.py gets it.
const char kWebRemixVersion[] = "1.20260707.12.00";
const char kWebUa[] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
                      "Chrome/140.0.0.0 Safari/537.36";
const int kItags[] = {251, 250, 249, 140};  // Opus best first; AAC only without Opus

std::mutex g_visitor_mutex;
std::string g_visitor;

std::string visitor_data(Http &http) {
    std::lock_guard<std::mutex> lock(g_visitor_mutex);
    if (!g_visitor.empty()) return g_visitor;
    HttpRequest req;
    req.method = "POST";
    req.url = "https://music.youtube.com/youtubei/v1/search?prettyPrint=false";
    req.headers = {{"Content-Type", "application/json"}, {"User-Agent", kWebUa}, {"Origin", "https://music.youtube.com"}};
    req.body = std::string("{\"context\":{\"client\":{\"clientName\":\"WEB_REMIX\",\"clientVersion\":\"") +
               kWebRemixVersion + "\",\"hl\":\"en\",\"gl\":\"US\"}},\"query\":\"test\"}";
    HttpResponse r = http.send(req);
    JsonDoc doc;
    std::string err;
    if (r.ok() && JsonDoc::parse(r.body, doc, err)) g_visitor = doc.root()["responseContext"]["visitorData"].string();
    if (g_visitor.empty())
        log_warn("resolve: no visitor data (" + (r.error.empty() ? "http " + std::to_string(r.status) : r.error) +
                 "); asking /player without it");
    return g_visitor;
}

std::string player_body(const PlayerClient &c, const std::string &video_id, const std::string &visitor) {
    std::string ctx = "{\"clientName\":" + json_quote(c.name) + ",\"clientVersion\":" + json_quote(c.version) +
                      ",\"hl\":\"en\",\"gl\":\"US\",\"userAgent\":" + json_quote(c.user_agent);
    if (!visitor.empty()) ctx += ",\"visitorData\":" + json_quote(visitor);
    const std::pair<const char *, const char *> extra[] = {
        {"deviceMake", c.device_make}, {"deviceModel", c.device_model}, {"osName", c.os_name}, {"osVersion", c.os_version}};
    for (auto [key, value] : extra)
        if (*value) ctx += std::string(",\"") + key + "\":" + json_quote(value);
    if (c.android_sdk) ctx += ",\"androidSdkVersion\":" + std::to_string(c.android_sdk);
    return "{\"context\":{\"client\":" + ctx + "}},\"videoId\":" + json_quote(video_id) +
           ",\"contentCheckOk\":true,\"racyCheckOk\":true}";
}

}  // namespace

void set_visitor_data(std::string visitor) {
    std::lock_guard<std::mutex> lock(g_visitor_mutex);
    g_visitor = std::move(visitor);
}

bool parse_player_response(std::string_view json, const PlayerClient &client, StreamInfo &out, std::string &err) {
    const std::string name = client.name;
    JsonDoc doc;
    std::string e;
    if (!JsonDoc::parse(json, doc, e)) {
        err = name + ": bad JSON: " + e;
        return false;
    }
    Json root = doc.root();
    Json play = root["playabilityStatus"];
    if (play["status"].str() != "OK") {
        err = name + ": " + (play["status"].is_string() ? play["status"].string() : "no playabilityStatus");
        if (!play["reason"].str().empty()) err += ": " + play["reason"].string();
        return false;
    }
    Json sd = root["streamingData"], fmt;
    for (int itag : kItags) {
        for (Json f : sd["adaptiveFormats"].items())
            if (f["itag"].i64() == itag) {
                fmt = f;
                break;
            }
        if (!fmt.is_null()) break;
    }
    if (fmt.is_null()) {
        err = name + ": no Opus or AAC audio format";
        return false;
    }
    StreamInfo si;
    si.video_id = root["videoDetails"]["videoId"].string();
    si.client = name;
    si.itag = int(fmt["itag"].i64());
    si.mime = fmt["mimeType"].string();
    si.content_length = fmt["contentLength"].i64();
    si.duration_ms = fmt["approxDurationMs"].i64();
    si.last_modified = fmt["lastModified"].i64();
    si.init_end = fmt["initRange"]["end"].i64();
    si.index_start = fmt["indexRange"]["start"].i64();
    si.index_end = fmt["indexRange"]["end"].i64();
    si.url = fmt["url"].string();
    si.sabr_url = sd["serverAbrStreamingUrl"].string();
    si.ustreamer_config = root["playerConfig"].find("videoPlaybackUstreamerConfig").string();
    si.user_agent = client.user_agent;
    si.client_version = client.version;
    si.client_id = client.id;
    si.loudness_db = fmt["loudnessDb"].num(root["playerConfig"]["audioConfig"]["loudnessDb"].num(0));
    if (!si.url.empty() && (si.content_length <= 0 || si.index_end <= 0)) {
        log_warn("resolve: " + name + " itag " + std::to_string(si.itag) + " has a url but no ranges; not DASH");
        si.url.clear();
    }
    if (si.sabr_url.empty() || si.ustreamer_config.empty() || si.last_modified <= 0) {
        si.sabr_url.clear();
        si.ustreamer_config.clear();
    }
    if (si.url.empty() && si.sabr_url.empty()) {
        err = name + ": itag " + std::to_string(si.itag) + " has neither a direct url nor SABR";
        return false;
    }
    out = std::move(si);
    return true;
}

bool resolve_client(const std::string &video_id, const PlayerClient &c, StreamInfo &out, std::string &err) {
    Http http;
    const std::string visitor = visitor_data(http);
    HttpRequest req;
    req.method = "POST";
    req.url = "https://www.youtube.com/youtubei/v1/player?prettyPrint=false";
    req.headers = {{"Content-Type", "application/json"},
                   {"User-Agent", c.user_agent},
                   {"X-YouTube-Client-Name", std::to_string(c.id)},
                   {"X-YouTube-Client-Version", c.version},
                   {"Origin", "https://www.youtube.com"}};
    if (!visitor.empty()) req.headers.emplace_back("X-Goog-Visitor-Id", visitor);
    req.body = player_body(c, video_id, visitor);
    HttpResponse r = http.send(req);
    StreamInfo si;
    if (!r.error.empty()) {
        err = std::string(c.name) + ": " + r.error;
    } else if (r.status != 200) {
        err = std::string(c.name) + ": http " + std::to_string(r.status);
    } else if (parse_player_response(r.body, c, si, err)) {
        if (si.video_id.empty()) si.video_id = video_id;
        if (si.video_id == video_id) {
            out = std::move(si);
            return true;
        }
        err = std::string(c.name) + ": answered for " + si.video_id;
    }
    return false;
}

bool resolve(const std::string &video_id, StreamInfo &out, std::string &err) {
    std::string errs;
    StreamInfo sabr;
    for (const PlayerClient &c : player_clients()) {
        StreamInfo si;
        std::string e;
        if (resolve_client(video_id, c, si, e)) {
            if (!si.url.empty()) {
                out = std::move(si);
                return true;
            }
            if (sabr.sabr_url.empty()) sabr = std::move(si);
            continue;
        }
        log_info("resolve " + video_id + ": " + e);
        errs += (errs.empty() ? "" : "; ") + e;
    }
    if (!sabr.sabr_url.empty()) {
        out = std::move(sabr);
        return true;
    }
    err = "resolve " + video_id + ": " + errs;
    return false;
}

}  // namespace bbm
