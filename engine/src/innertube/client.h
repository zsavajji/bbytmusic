// The YouTube Music (WEB_REMIX) InnerTube client: the request context, the headers and the
// visitor data. Anonymous only: no cookies, no sign-in. One InnerTube per thread (it owns an Http).
#pragma once
#include <string>
#include <string_view>

#include "net/http.h"
#include "util/json.h"

namespace bbm {

// The web client the search and next requests identify as. The version was checked against the
// live service on 2026-11-14; YouTube accepts old versions for a long time, but bump it if
// responses start to change shape.
inline constexpr const char *kClientName = "WEB_REMIX";
inline constexpr const char *kClientNameId = "67";   // X-YouTube-Client-Name
inline constexpr const char *kClientVersion = "1.20260707.12.00";

class InnerTube {
public:
    void set_language(std::string hl, std::string gl) { hl_ = std::move(hl); gl_ = std::move(gl); }
    const std::string &hl() const { return hl_; }
    const std::string &gl() const { return gl_; }

    // Taken from the first response's responseContext.visitorData when not set; sent as
    // context.client.visitorData and X-Goog-Visitor-Id afterwards. The streaming module shares it.
    const std::string &visitor_data() const { return visitor_; }
    void set_visitor_data(std::string v) { visitor_ = std::move(v); }

    // POST https://music.youtube.com/youtubei/v1/<endpoint>?prettyPrint=false with the body
    // {"context":{"client":{...}},<body_fields>}. body_fields is the inside of a JSON object
    // (e.g. "\"query\":\"x\""), or empty. On success `out` holds the parsed response.
    bool post(std::string_view endpoint, std::string_view body_fields, JsonDoc &out, std::string &err);

    Http &http() { return http_; }

private:
    Http http_;
    std::string hl_ = "en", gl_ = "US", visitor_;
};

}  // namespace bbm
