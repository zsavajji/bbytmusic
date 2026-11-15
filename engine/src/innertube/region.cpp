#include "innertube/region.h"

namespace bbm {

bool valid_region(std::string_view s) {
    return s.size() == 2 && s[0] >= 'A' && s[0] <= 'Z' && s[1] >= 'A' && s[1] <= 'Z';
}

bool parse_sw_data(const std::string &body, std::string &gl, std::string &err) {
    std::string_view text = body;
    if (text.substr(0, 4) == ")]}'") text.remove_prefix(4);
    JsonDoc doc;
    std::string perr;
    if (!JsonDoc::parse(text, doc, perr)) {
        err = "sw.js_data: bad JSON: " + perr;
        return false;
    }
    const Json c = doc.root()[0][2][0][0][1];
    if (!c.is_string() || !valid_region(c.str())) {
        err = "sw.js_data: no country in the answer";
        return false;
    }
    gl = c.string();
    return true;
}

bool fetch_region(InnerTube &yt, std::string &gl, std::string &err) {
    HttpRequest req;
    req.url = "https://music.youtube.com/sw.js_data";
    req.headers = {{"User-Agent", kWebUserAgent}};
    req.timeout_s = 8;   // a call waits for this on the first start, no longer
    HttpResponse resp = yt.http().send(req);
    if (!resp.error.empty()) {
        err = "sw.js_data: " + resp.error;
        return false;
    }
    if (resp.status < 200 || resp.status >= 300) {
        err = "sw.js_data: HTTP " + std::to_string(resp.status);
        return false;
    }
    return parse_sw_data(resp.body, gl, err);
}

}  // namespace bbm
