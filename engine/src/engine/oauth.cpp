#include "engine/oauth.h"

#include "util/json.h"

namespace bbm {
namespace {

const char *const kTvUserAgent =
    "Mozilla/5.0 (ChromiumStylePlatform) Cobalt/25.lts.30.1034943-gold (unlike Gecko), Unknown_TV_Unknown_0/Unknown (Unknown, Unknown)";
const char *const kOrigin = "https://www.youtube.com";
const char *const kTvVersion = "7.20260707.07.00";

bool word(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool ends_with(std::string_view s, std::string_view suffix) { return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix; }

HttpRequest request(const std::string &method, const std::string &url) {
    HttpRequest r;
    r.method = method;
    r.url = url;
    r.timeout_s = 15;
    r.headers.push_back({"User-Agent", kTvUserAgent});
    return r;
}

bool transport_failed(const HttpResponse &r) { return !r.error.empty() || r.status == 0; }

}  // namespace

std::string tv_base_js_url(std::string_view page) {
    std::string url;
    size_t at = page.find("id=\"base-js\"");
    if (at != std::string_view::npos) {
        at = page.find("src=\"", at);
        const size_t end = at == std::string_view::npos ? at : page.find('"', at + 5);
        if (end != std::string_view::npos) url = std::string(page.substr(at + 5, end - at - 5));
    }
    for (size_t from = 0; url.empty();) {   // the fallback: .src = '…m=base'
        at = page.find(".src = '", from);
        if (at == std::string_view::npos) break;
        const size_t end = page.find('\'', at + 8);
        if (end == std::string_view::npos) break;
        const std::string_view u = page.substr(at + 8, end - at - 8);
        if (ends_with(u, "m=base")) url = std::string(u);
        from = end;
    }
    if (url.empty()) return "";
    for (size_t i; (i = url.find("\\/")) != std::string::npos;) url.erase(i, 1);
    if (url[0] == '/' && url.compare(0, 2, "//") != 0) url = kOrigin + url;
    return url;
}

bool parse_tv_client(std::string_view js, OAuthClient &out) {
    static const std::string_view key = "clientId:\"", suffix = ".apps.googleusercontent.com";
    for (size_t at = 0; (at = js.find(key, at)) != std::string_view::npos;) {
        at += key.size();
        const size_t end = js.find('"', at);
        if (end == std::string_view::npos) return false;
        const std::string_view id = js.substr(at, end - at);
        size_t i = end + 1;
        if (id.size() <= suffix.size() || !ends_with(id, suffix) || i >= js.size() || js[i] != ',') continue;
        for (i++; i < js.size() && (js[i] == '\n' || js[i] == '\r' || js[i] == ' '); i++) {}
        const size_t name = i;
        while (i < js.size() && (word(js[i]) || js[i] == '$')) i++;
        if (i == name || js.compare(i, 2, ":\"") != 0) continue;
        const size_t s0 = i += 2;
        while (i < js.size() && word(js[i])) i++;
        if (i == s0 || i >= js.size() || js[i] != '"') continue;
        out.id = std::string(id);
        out.secret = std::string(js.substr(s0, i - s0));
        return true;
    }
    return false;
}

bool fetch_tv_client(const HttpSendFn &send, OAuthClient &out, std::string &err) {
    HttpResponse r = send(request("GET", std::string(kOrigin) + "/tv"));
    if (!r.ok()) {
        err = r.error.empty() ? "tv page: HTTP " + std::to_string(r.status) : "tv page: " + r.error;
        return false;
    }
    const std::string url = tv_base_js_url(r.body);
    if (url.empty()) {
        err = "tv page: no script";
        return false;
    }
    HttpRequest js = request("GET", url);
    js.timeout_s = 60;   // megabytes of script, and the phone may be on a slow mobile link
    r = send(js);
    if (!r.ok()) {
        err = r.error.empty() ? "tv script: HTTP " + std::to_string(r.status) : "tv script: " + r.error;
        return false;
    }
    if (!parse_tv_client(r.body, out)) {
        err = "tv script: no client";
        return false;
    }
    return true;
}

bool parse_token(std::string_view body, OAuthToken &out, std::string &err) {
    JsonDoc doc;
    if (!JsonDoc::parse(body, doc, err)) return false;
    const Json root = doc.root();
    if (root["error"].is_string() && !root["error"].str().empty()) {
        err = root["error"].string();
        return false;
    }
    if (!root["access_token"].is_string() || root["access_token"].str().empty()) {
        err = "no access_token";
        return false;
    }
    out.access = root["access_token"].string();
    out.refresh = root["refresh_token"].string();
    out.expires_in = root["expires_in"].i64();
    return true;
}

OAuthResult refresh_token(const HttpSendFn &send, const OAuthClient &c, const std::string &refresh, OAuthToken &out, std::string &err) {
    HttpRequest req = request("POST", std::string(kOrigin) + "/o/oauth2/token");
    req.headers.push_back({"Content-Type", "application/json"});
    req.body = "{\"refresh_token\":" + json_quote(refresh) + ",\"client_id\":" + json_quote(c.id) + ",\"client_secret\":" +
               json_quote(c.secret) + ",\"grant_type\":\"refresh_token\"}";
    const HttpResponse r = send(req);
    if (transport_failed(r) || r.status >= 500) {
        err = r.error.empty() ? "token: HTTP " + std::to_string(r.status) : "token: " + r.error;
        return OAuthResult::Network;
    }
    JsonDoc doc;
    std::string e;
    if (!JsonDoc::parse(r.body, doc, e)) {
        err = "token: HTTP " + std::to_string(r.status) + ", " + e;
        return OAuthResult::Network;
    }
    out = {};
    if (parse_token(r.body, out, e)) return OAuthResult::Ok;
    err = e;
    if (e == "invalid_grant" || e == "invalid_client" || e == "unauthorized_client" || r.status == 400 || r.status == 401)
        return OAuthResult::Refused;
    return OAuthResult::Network;
}

bool parse_account(std::string_view body, AccountInfo &out) {
    JsonDoc doc;
    std::string err;
    if (!JsonDoc::parse(body, doc, err)) return false;
    Json pick;
    bool selected = false;
    for (Json a : doc.root()["contents"].items())
        for (Json b : a["accountSectionListRenderer"]["contents"].items())
            for (Json c : b["accountItemSectionRenderer"]["contents"].items()) {
                const Json item = c["accountItem"];
                if (!item.is_object()) continue;
                const bool sel = item["isSelected"].boolean();
                if (pick.is_null() || (sel && !selected)) {
                    pick = item;
                    selected = sel;
                }
            }
    if (pick.is_null()) return false;
    out.name = pick["accountName"]["simpleText"].string();
    out.photo = pick["accountPhoto"]["thumbnails"][0]["url"].string();
    return true;
}

OAuthResult fetch_account(const HttpSendFn &send, const std::string &access, AccountInfo &out, std::string &err) {
    HttpRequest req = request("POST", std::string(kOrigin) + "/youtubei/v1/account/accounts_list?prettyPrint=false");
    req.headers.push_back({"Content-Type", "application/json"});
    req.headers.push_back({"Authorization", "Bearer " + access});
    req.headers.push_back({"Origin", kOrigin});
    req.headers.push_back({"X-YouTube-Client-Name", "7"});
    req.headers.push_back({"X-YouTube-Client-Version", kTvVersion});
    req.body = std::string("{\"context\":{\"client\":{\"clientName\":\"TVHTML5\",\"clientVersion\":\"") + kTvVersion +
               "\",\"hl\":\"en\",\"gl\":\"US\"}},\"accountReadMask\":{\"returnOwner\":true,\"returnBrandAccounts\":true,"
               "\"returnPersonaAccounts\":false}}";
    const HttpResponse r = send(req);
    if (!transport_failed(r) && (r.status == 401 || r.status == 403)) {
        err = "accounts: HTTP " + std::to_string(r.status);
        return OAuthResult::Refused;
    }
    if (!r.ok()) {
        err = r.error.empty() ? "accounts: HTTP " + std::to_string(r.status) : "accounts: " + r.error;
        return OAuthResult::Network;
    }
    if (!parse_account(r.body, out)) {
        err = "accounts: no account in the answer";
        return OAuthResult::Network;
    }
    return OAuthResult::Ok;
}

OAuthResult request_device_code(const HttpSendFn &send, const OAuthClient &c, const std::string &device_id, DeviceCode &out, std::string &err) {
    HttpRequest req = request("POST", std::string(kOrigin) + "/o/oauth2/device/code");
    req.headers.push_back({"Content-Type", "application/json"});
    req.body = "{\"client_id\":" + json_quote(c.id) + ",\"device_id\":" + json_quote(device_id) +
               ",\"model_name\":\"ytlr::\",\"scope\":\"http://gdata.youtube.com https://www.googleapis.com/auth/youtube-paid-content\"}";
    const HttpResponse r = send(req);
    if (transport_failed(r) || r.status >= 500) {
        err = r.error.empty() ? "device code: HTTP " + std::to_string(r.status) : "device code: " + r.error;
        return OAuthResult::Network;
    }
    JsonDoc doc;
    std::string e;
    if (!JsonDoc::parse(r.body, doc, e)) {
        err = "device code: HTTP " + std::to_string(r.status) + ", " + e;
        return OAuthResult::Network;
    }
    const Json j = doc.root();
    if (!j["device_code"].is_string() || j["device_code"].str().empty() || !j["user_code"].is_string() || j["user_code"].str().empty()) {
        err = j["error"].is_string() && !j["error"].str().empty() ? j["error"].string() : "device code: HTTP " + std::to_string(r.status);
        return OAuthResult::Refused;
    }
    out.device_code = j["device_code"].string();
    out.user_code = j["user_code"].string();
    out.url = j["verification_url"].string();
    if (!j["interval"].is_null() && j["interval"].i64() >= 0) out.interval = j["interval"].i64();
    if (j["expires_in"].i64() > 0) out.expires_in = j["expires_in"].i64();
    return OAuthResult::Ok;
}

PollResult poll_device_token(const HttpSendFn &send, const OAuthClient &c, const std::string &device_code, OAuthToken &out, std::string &err) {
    HttpRequest req = request("POST", std::string(kOrigin) + "/o/oauth2/token");
    req.headers.push_back({"Content-Type", "application/json"});
    req.body = "{\"code\":" + json_quote(device_code) + ",\"client_id\":" + json_quote(c.id) + ",\"client_secret\":" +
               json_quote(c.secret) + ",\"grant_type\":\"http://oauth.net/grant_type/device/1.0\"}";
    const HttpResponse r = send(req);
    if (transport_failed(r) || r.status >= 500) {
        err = r.error.empty() ? "token: HTTP " + std::to_string(r.status) : "token: " + r.error;
        return PollResult::Network;
    }
    JsonDoc doc;
    std::string e;
    if (!JsonDoc::parse(r.body, doc, e)) {
        err = "token: HTTP " + std::to_string(r.status) + ", " + e;
        return PollResult::Network;
    }
    out = {};
    if (parse_token(r.body, out, e)) {
        if (out.refresh.empty()) {
            err = "no refresh_token";
            return PollResult::Refused;
        }
        return PollResult::Ok;
    }
    if (e == "authorization_pending") return PollResult::Pending;
    if (e == "slow_down") return PollResult::SlowDown;
    err = e;
    return PollResult::Refused;
}

bool revoke_token(const HttpSendFn &send, const std::string &token, std::string &err) {
    static const char *hex = "0123456789ABCDEF";
    std::string enc;
    for (unsigned char ch : token) {
        if (word(static_cast<char>(ch)) || ch == '.' || ch == '~' || ch == '-') enc += static_cast<char>(ch);
        else {
            enc += '%';
            enc += hex[ch >> 4];
            enc += hex[ch & 15];
        }
    }
    HttpRequest req = request("POST", "https://oauth2.googleapis.com/revoke?token=" + enc);
    req.headers.push_back({"Content-Type", "application/json"});
    req.body = "{}";
    const HttpResponse r = send(req);
    if (r.status == 200 && r.error.empty()) return true;
    err = r.error.empty() ? "revoke: HTTP " + std::to_string(r.status) : "revoke: " + r.error;
    return false;
}

}  // namespace bbm
