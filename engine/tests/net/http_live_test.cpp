// Needs the network: GETs and POSTs to music.youtube.com. ctest label `live`.
#include "check.h"
#include "net/http.h"

using namespace bbm;

static const char *kUa =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) "
    "Version/26.0 Safari/605.1.15";

int main() {
    Http http;
    HttpRequest r;
    r.url = "https://music.youtube.com/";
    r.headers = {{"User-Agent", kUa}};
    HttpResponse resp = http.send(r);
    std::printf("GET: status %ld, %zu bytes, error '%s'\n", resp.status, resp.body.size(), resp.error.c_str());
    CHECK(resp.error.empty());
    CHECK(resp.status > 0 && resp.status < 400);
    CHECK(!resp.body.empty());

    HttpRequest s;
    s.method = "POST";
    s.url = "https://music.youtube.com/youtubei/v1/search?prettyPrint=false";
    s.headers = {{"Content-Type", "application/json"}, {"Origin", "https://music.youtube.com"}, {"User-Agent", kUa}};
    s.body = R"({"context":{"client":{"clientName":"WEB_REMIX","clientVersion":"1.20260707.12.00","hl":"en","gl":"US"}},"query":"never gonna give you up"})";
    resp = http.send(s);   // same handle: keep-alive
    std::printf("POST: status %ld, %zu bytes, error '%s'\n", resp.status, resp.body.size(), resp.error.c_str());
    CHECK_EQ(resp.status, 200L);
    CHECK(resp.body.find("visitorData") != std::string::npos);

    size_t got = 0;
    HttpRequest g;
    g.url = "https://music.youtube.com/";
    g.headers = {{"User-Agent", kUa}};
    g.on_data = [&](const uint8_t *, size_t n) { got += n; return true; };
    resp = http.send(g);
    std::printf("streamed GET: status %ld, %zu bytes\n", resp.status, got);
    CHECK(resp.status > 0 && resp.status < 400);
    CHECK(got > 0);
    CHECK(resp.body.empty());
    return check_done();
}
