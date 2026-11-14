#include "engine/sponsor.h"

#include <algorithm>
#include <cmath>

#include "util/json.h"
#include "util/sha256.h"

namespace bbm {

const std::vector<std::string> &skip_categories() {
    static const std::vector<std::string> all = {"sponsor", "intro", "outro", "music_offtopic"};
    return all;
}

std::string sponsor_url(const std::string &video_id, const std::vector<std::string> &categories) {
    std::string cats;
    for (const std::string &c : categories) cats += (cats.empty() ? "%22" : ",%22") + c + "%22";   // names are ours: no escaping
    return "https://sponsor.ajay.app/api/skipSegments/" + sha256_hex(video_id).substr(0, 4) + "?categories=%5B" + cats +
           "%5D&actionTypes=%5B%22skip%22%5D";
}

bool parse_segments(std::string_view body, const std::string &video_id, const std::vector<std::string> &categories,
                    std::vector<SkipSegment> &out, std::string &err) {
    out.clear();
    JsonDoc doc;
    if (!JsonDoc::parse(body, doc, err)) return false;
    if (!doc.root().is_array()) {
        err = "not an array";
        return false;
    }
    for (Json v : doc.root().items()) {
        if (v["videoID"].str() != video_id) continue;
        for (Json s : v["segments"].items()) {
            const std::string cat = s["category"].string();
            const Json t = s["segment"];
            if (s["actionType"].str() != "skip" || std::find(categories.begin(), categories.end(), cat) == categories.end()) continue;
            if (!t[0].is_number() || !t[1].is_number()) continue;
            out.push_back({cat, int64_t(std::llround(t[0].num() * 1000)), int64_t(std::llround(t[1].num() * 1000))});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const SkipSegment &a, const SkipSegment &b) { return a.from_ms < b.from_ms; });
    std::vector<SkipSegment> merged;
    for (const SkipSegment &s : out) {
        if (s.to_ms <= s.from_ms) continue;
        if (!merged.empty() && s.from_ms <= merged.back().to_ms) merged.back().to_ms = std::max(merged.back().to_ms, s.to_ms);
        else merged.push_back(s);
    }
    out = std::move(merged);
    return true;
}

bool fetch_segments(Http &http, const std::string &video_id, const std::vector<std::string> &categories,
                    std::vector<SkipSegment> &out, std::string &err) {
    out.clear();
    HttpRequest req;
    req.url = sponsor_url(video_id, categories);
    req.timeout_s = 10;
    const HttpResponse r = http.send(req);
    if (r.status == 404 && r.error.empty()) return true;
    if (!r.ok()) {
        err = r.error.empty() ? "sponsor: HTTP " + std::to_string(r.status) : "sponsor: " + r.error;
        return false;
    }
    return parse_segments(r.body, video_id, categories, out, err);
}

}  // namespace bbm
