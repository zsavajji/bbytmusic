// Gate 0.3 (PLAN.md §9), the part that needs no .bar: a C++20 program built with bb10-clang++
// runs on the phone and reaches Google over TLS 1.3 through libcurl + wolfSSL, with our own
// roots. It then walks the DASH path once from the phone's network: search (visitor data),
// /player as VISIONOS, 64 KB of itag 251.
//
//   gate03 <cacert.pem> [http://127.0.0.1:8899]     (the proxy: tools/gates/connect_proxy.py)
#include <curl/curl.h>

#include <chrono>
#include <cstdio>
#include <format>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

const char *kSafariUa =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) "
    "Version/26.0 Safari/605.1.15";

size_t collect(char *data, size_t size, size_t n, void *out) {
    static_cast<std::string *>(out)->append(data, size * n);
    return size * n;
}

struct Reply {
    long status = 0;
    std::string body;
    std::string error;
    double seconds = 0;
};

const char *g_proxy = nullptr;  // a CONNECT proxy, for a phone with no network of its own

Reply request(const std::string &url, const char *ca, const std::vector<std::string> &headers,
              const std::string *post = nullptr) {
    Reply reply;
    CURL *c = curl_easy_init();
    char err[CURL_ERROR_SIZE] = "";
    curl_slist *list = nullptr;
    for (const auto &h : headers) list = curl_slist_append(list, h.c_str());
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_CAINFO, ca);
    curl_easy_setopt(c, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_3));  // 1.3 or fail
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &reply.body);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    if (g_proxy) curl_easy_setopt(c, CURLOPT_PROXY, g_proxy);
    if (post) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, post->c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(post->size()));
    }
    const auto t0 = std::chrono::steady_clock::now();
    const CURLcode rc = curl_easy_perform(c);
    reply.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (rc != CURLE_OK) reply.error = std::format("{} ({})", err[0] ? err : curl_easy_strerror(rc), int(rc));
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &reply.status);
    curl_slist_free_all(list);
    curl_easy_cleanup(c);
    return reply;
}

// The string value that follows `key` (as "key":"value") after position `from`.
std::string value_after(const std::string &json, const std::string &key, size_t from = 0) {
    const std::string needle = "\"" + key + "\":\"";
    const size_t a = json.find(needle, from);
    if (a == std::string::npos) return {};
    const size_t b = json.find('"', a + needle.size());
    std::string v = json.substr(a + needle.size(), b - a - needle.size());
    for (size_t p; (p = v.find("\\u0026")) != std::string::npos;) v.replace(p, 6, "&");
    return v;
}

bool step(const char *name, bool ok, const std::string &detail) {
    std::printf("%-28s %s  %s\n", name, ok ? "ok  " : "FAIL", detail.c_str());
    std::fflush(stdout);
    return ok;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::puts("usage: gate03 <cacert.pem>");
        return 0;  // push-phone.sh runs it bare to see that the phone will execute it
    }
    const char *ca = argv[1];
    if (argc > 2) g_proxy = argv[2];
    bool all = true;

    // The C++ runtime: an exception through frames with cleanups, a thread, <format>.
    try {
        std::string s(100, 'x');
        throw std::runtime_error(std::format("thrown with {} bytes alive", s.size()));
    } catch (const std::exception &e) {
        all &= step("exception", true, e.what());
    }
    int from_thread = 0;
    std::thread([&] { from_thread = 42; }).join();
    all &= step("std::thread", from_thread == 42, std::format("value {}", from_thread));

    curl_global_init(CURL_GLOBAL_DEFAULT);
    const curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
    all &= step("libcurl", true, std::format("{} with {}", v->version, v->ssl_version ? v->ssl_version : "no TLS"));

    // A page that only needs TLS: 1.3 is forced, so a 2xx/3xx here is a 1.3 handshake.
    Reply home = request("https://music.youtube.com/", ca, {std::string("User-Agent: ") + kSafariUa});
    all &= step("TLS 1.3 to music.youtube", home.error.empty() && home.status > 0,
                home.error.empty() ? std::format("http {}, {} bytes, {:.2f} s", home.status, home.body.size(), home.seconds)
                                   : home.error);

    // InnerTube search, for the visitor data every later call carries.
    const std::string search_body =
        R"({"context":{"client":{"clientName":"WEB_REMIX","clientVersion":"1.20260707.12.00","hl":"en","gl":"US"}},"query":"never gonna give you up"})";
    Reply search = request("https://music.youtube.com/youtubei/v1/search?prettyPrint=false", ca,
                           {"Content-Type: application/json", "Origin: https://music.youtube.com"}, &search_body);
    const std::string visitor = value_after(search.body, "visitorData");
    all &= step("search (WEB_REMIX)", search.status == 200 && !visitor.empty(),
                search.error.empty() ? std::format("http {}, {} bytes, visitor data {}, {:.2f} s", search.status,
                                                   search.body.size(), visitor.empty() ? "missing" : "yes", search.seconds)
                                     : search.error);

    // /player as VISIONOS: the client gate 0.1 found serving direct urls with no PO token.
    const std::string player_body = std::format(
        R"({{"context":{{"client":{{"clientName":"VISIONOS","clientVersion":"1.02","hl":"en","gl":"US","userAgent":"{}","visitorData":"{}","deviceMake":"Apple","deviceModel":"RealityDevice17,1","osName":"visionOS","osVersion":"26.5.23O471"}}}},"videoId":"dQw4w9WgXcQ","contentCheckOk":true,"racyCheckOk":true}})",
        kSafariUa, visitor);
    Reply player = request("https://www.youtube.com/youtubei/v1/player?prettyPrint=false", ca,
                           {"Content-Type: application/json", std::string("User-Agent: ") + kSafariUa,
                            "X-YouTube-Client-Name: 101", "X-YouTube-Client-Version: 1.02",
                            "X-Goog-Visitor-Id: " + visitor, "Origin: https://www.youtube.com"},
                           &player_body);
    const size_t opus = player.body.find("\"itag\":251");
    const std::string url = opus == std::string::npos ? std::string() : value_after(player.body, "url", opus);
    const bool sabr = player.body.find("serverAbrStreamingUrl") != std::string::npos;
    all &= step("/player (VISIONOS)", player.status == 200 && !url.empty(),
                player.error.empty() ? std::format("http {}, {} bytes, playability {}, itag 251 url {}, SABR url {}, {:.2f} s",
                                                   player.status, player.body.size(),
                                                   value_after(player.body, "status", player.body.find("playabilityStatus")),
                                                   url.empty() ? "missing" : "yes", sabr ? "yes" : "no", player.seconds)
                                     : player.error);

    if (!url.empty()) {
        Reply media = request(url, ca, {std::string("User-Agent: ") + kSafariUa, "Range: bytes=0-65535"});
        all &= step("64 KB of itag 251", media.status == 206 && media.body.size() == 65536,
                    media.error.empty() ? std::format("http {}, {} bytes, {:.2f} s, first bytes {:02x}{:02x}{:02x}{:02x}",
                                                      media.status, media.body.size(), media.seconds,
                                                      (unsigned char)media.body[0], (unsigned char)media.body[1],
                                                      (unsigned char)media.body[2], (unsigned char)media.body[3])
                                        : media.error);
    }
    curl_global_cleanup();
    std::puts(all ? "gate03: all passed" : "gate03: FAILED");
    return all ? 0 : 1;
}
