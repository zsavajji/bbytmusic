#include "innertube/client.h"

namespace bbm {

bool InnerTube::post(std::string_view endpoint, std::string_view body_fields, JsonDoc &out,
                     std::string &err) {
    std::string body = std::string("{\"context\":{\"client\":{\"clientName\":\"") + kClientName +
                       "\",\"clientVersion\":\"" + kClientVersion + "\",\"hl\":" + json_quote(hl_) +
                       ",\"gl\":" + json_quote(gl_);
    if (!visitor_.empty()) body += ",\"visitorData\":" + json_quote(visitor_);
    body += "}}";
    if (!body_fields.empty()) {
        body += ',';
        body += body_fields;
    }
    body += '}';

    HttpRequest req;
    req.method = "POST";
    req.url = "https://music.youtube.com/youtubei/v1/" + std::string(endpoint) + "?prettyPrint=false";
    req.headers = {{"Content-Type", "application/json"},
                   {"User-Agent", kWebUserAgent},
                   {"Origin", "https://music.youtube.com"},
                   {"X-YouTube-Client-Name", kClientNameId},
                   {"X-YouTube-Client-Version", kClientVersion}};
    if (!visitor_.empty()) req.headers.emplace_back("X-Goog-Visitor-Id", visitor_);
    req.body = std::move(body);

    HttpResponse resp = http_.send(req);
    if (!resp.error.empty()) {
        err = std::string(endpoint) + ": " + resp.error;
        return false;
    }
    if (resp.status < 200 || resp.status >= 300) {
        err = std::string(endpoint) + ": HTTP " + std::to_string(resp.status);
        return false;
    }
    std::string perr;
    if (!JsonDoc::parse(resp.body, out, perr)) {
        err = std::string(endpoint) + ": bad JSON: " + perr;
        return false;
    }
    if (visitor_.empty()) visitor_ = out.root()["responseContext"]["visitorData"].string();
    return true;
}

}  // namespace bbm
