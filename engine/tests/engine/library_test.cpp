// The account's library, Liked Music and likes through a real Engine and its socket, against a fake TV service; no network.
#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "audio/null_sink.h"
#include "check.h"
#include "engine/client.h"
#include "engine/engine.h"
#include "player/support.h"

using namespace bbm;
using etest::Client;
using etest::parse;

namespace {

std::string fixture(const char *name) {
    std::ifstream f(std::string(BBM_FIXTURES) + "/innertube/tv/" + name, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The service: the token endpoint, the account list, the TV browse and like endpoints.
struct Fake {
    std::atomic<int> token{0}, fail401{0};
    std::mutex mu;
    std::string last_auth, last_body;
    std::vector<std::string> urls;
    HttpSendFn fn() {
        return [this](const HttpRequest &r) {
            HttpResponse o;
            o.status = 200;
            if (r.url == "https://www.youtube.com/o/oauth2/token") {
                token++;
                o.body = "{\"access_token\":\"ACCESS-" + std::to_string(token.load()) + "\",\"expires_in\":3600}";
            } else if (r.url.find("accounts_list") != std::string::npos) {
                o.body = "{\"contents\":[{\"accountSectionListRenderer\":{\"contents\":[{\"accountItemSectionRenderer\":{\"contents\":["
                         "{\"accountItem\":{\"accountName\":{\"simpleText\":\"Test User\"},\"isSelected\":true}}]}}]}}]}";
            } else if (r.url.rfind("https://music.youtube.com/youtubei/v1/browse", 0) == 0 ||
                       r.url.rfind("https://www.youtube.com/youtubei/v1/like/", 0) == 0 ||
                       r.url.rfind("https://www.youtube.com/youtubei/v1/next", 0) == 0) {
                {
                    std::lock_guard<std::mutex> lk(mu);
                    urls.push_back(r.url);
                    last_body = r.body;
                    last_auth.clear();
                    for (auto &h : r.headers)
                        if (h.first == "Authorization") last_auth = h.second;
                }
                if (fail401 > 0) {
                    fail401--;
                    o.status = 401;
                } else if (r.url.find("/like/") != std::string::npos) {
                    o.body = "{}";
                } else if (r.url.find("/next") != std::string::npos) {   // the first video is liked, any other is not
                    o.body = std::string("{\"contents\":{\"x\":[{\"button\":{\"likeButtonRenderer\":{\"likeStatus\":\"") +
                             (r.body.find("vid00001AAA") != std::string::npos ? "LIKE" : "INDIFFERENT") + "\"}}}]}}";
                } else if (r.body.find("\"browseId\":\"FEmusic_liked_playlists\"") != std::string::npos) {
                    o.body = fixture("tv_library_playlists.json");
                } else if (r.body.find("\"browseId\":\"FEmusic_liked_albums\"") != std::string::npos) {
                    o.body = fixture("tv_library_empty.json");
                } else if (r.body.find("\"browseId\":\"VLLM\"") != std::string::npos) {
                    o.body = fixture("tv_playlist.json");
                } else if (r.body.find("\"continuation\":\"CONTINUATION_6\"") != std::string::npos) {
                    o.body = fixture("tv_playlist_more.json");
                } else if (r.body.find("\"continuation\"") != std::string::npos) {
                    o.body = fixture("tv_library_more.json");
                } else {
                    o.status = 404;
                }
            } else {
                o.status = 404;
            }
            return o;
        };
    }
    std::string auth() { std::lock_guard<std::mutex> lk(mu); return last_auth; }
    std::string body() { std::lock_guard<std::mutex> lk(mu); return last_body; }
    std::string last_url() { std::lock_guard<std::mutex> lk(mu); return urls.empty() ? "" : urls.back(); }
};

}  // namespace

int main() {
    char tmpl[] = "/tmp/bbm_library_XXXXXX";
    const std::string dir = mkdtemp(tmpl);
    Fake f;
    EngineOptions o;
    o.data_dir = dir;
    o.version = "test";
    o.sinks = [] { return std::make_unique<NullSink>(true); };
    o.account_http = f.fn();
    Engine e(std::move(o));
    std::string err;
    CHECK(e.start(err));
    std::thread t([&] { e.run(); });
    {
        Client c(e.port());
        CHECK(c.hello(e.token()));
        JsonDoc d;
        auto code = [&](const std::string &line) { return parse(line, d)["error"]["code"].string(); };

        // signed out
        CHECK_EQ(code(c.cmd("browse", "\"id\":\"FEmusic_liked_playlists\"")), std::string("signed_out"));
        CHECK_EQ(code(c.cmd("browse", "\"id\":\"VLLM\"")), std::string("signed_out"));
        CHECK_EQ(code(c.cmd("like", "\"id\":\"vid00001AAA\",\"on\":true")), std::string("signed_out"));
        CHECK(f.urls.empty());

        CHECK_EQ(parse(c.cmd("account", "\"import\":{\"refresh_token\":\"REFRESH-1\",\"client_id\":\"id-1\",\"client_secret\":\"sec-1\"}"), d)
                     ["result"]["state"].string(), std::string("in"));
        const int t0 = f.token;

        // a library tab
        {
            Json r = parse(c.cmd("browse", "\"id\":\"FEmusic_liked_playlists\""), d)["result"];
            CHECK_EQ(r["kind"].string(), std::string("feed"));
            CHECK_EQ(r["id"].string(), std::string("FEmusic_liked_playlists"));
            CHECK_EQ(r["sections"].size(), size_t(1));
            CHECK_EQ(r["sections"][0]["title"].string(), std::string("Playlists"));
            CHECK(r["sections"][0]["items"].size() > 0);
            CHECK_EQ(f.auth(), std::string("Bearer ACCESS-") + std::to_string(t0));
        }
        CHECK_EQ(f.token.load(), t0);
        CHECK_EQ(f.last_url(), std::string("https://music.youtube.com/youtubei/v1/browse?prettyPrint=false"));
        // an empty tab: no section
        {
            Json r = parse(c.cmd("browse", "\"id\":\"FEmusic_liked_albums\""), d)["result"];
            CHECK_EQ(r["kind"].string(), std::string("feed"));
            CHECK_EQ(r["sections"].size(), size_t(0));
            CHECK_EQ(r["continuation"].string(), std::string(""));
        }
        // Liked Music, and its next page
        std::string more;
        {
            Json r = parse(c.cmd("browse", "\"id\":\"VLLM\""), d)["result"];
            CHECK_EQ(r["kind"].string(), std::string("playlist"));
            CHECK_EQ(r["playlist_id"].string(), std::string("LM"));
            CHECK(r["items"].size() > 0);
            more = r["continuation"].string();
            CHECK_EQ(more.compare(0, 4, "tvp:"), 0);
        }
        {
            Json r = parse(c.cmd("browse_more", "\"continuation\":" + json_quote(more)), d)["result"];
            CHECK(r["items"].size() > 0);
            CHECK_EQ(r["sections"].size(), size_t(0));
            CHECK_EQ(f.body().find("\"continuation\":\"tvp:"), std::string::npos);
        }
        // a library continuation: one section, empty title
        {
            Json r = parse(c.cmd("browse_more", "\"continuation\":\"tvf:LIB-1\""), d)["result"];
            CHECK_EQ(r["sections"].size(), size_t(1));
            CHECK_EQ(r["sections"][0]["title"].string(), std::string(""));
            CHECK(r["sections"][0]["items"].size() > 0);
        }

        // a stale token: refreshed once, the second try answers
        f.fail401 = 1;
        {
            Json r = parse(c.cmd("browse", "\"id\":\"FEmusic_liked_playlists\""), d)["result"];
            CHECK_EQ(r["kind"].string(), std::string("feed"));
            CHECK_EQ(f.token.load(), t0 + 1);
            CHECK_EQ(f.auth(), std::string("Bearer ACCESS-") + std::to_string(t0 + 1));
        }
        f.fail401 = 2;   // twice: an error
        CHECK_EQ(code(c.cmd("browse", "\"id\":\"FEmusic_liked_playlists\"")), std::string("network"));
        f.fail401 = 0;

        // like
        CHECK(parse(c.cmd("like", "\"id\":\"vid00001AAA\",\"on\":true"), d)["ok"].boolean(true));
        CHECK_EQ(f.last_url(), std::string("https://www.youtube.com/youtubei/v1/like/like?prettyPrint=false"));
        CHECK(f.body().find("\"target\":{\"videoId\":\"vid00001AAA\"}") != std::string::npos);
        CHECK(parse(c.cmd("like", "\"id\":\"vid00001AAA\",\"on\":false"), d)["ok"].boolean(true));
        CHECK_EQ(f.last_url(), std::string("https://www.youtube.com/youtubei/v1/like/removelike?prettyPrint=false"));
        CHECK_EQ(code(c.cmd("like", "\"id\":\"a b\",\"on\":true")), std::string("bad_request"));
        CHECK_EQ(code(c.cmd("like", "\"id\":\"\",\"on\":true")), std::string("bad_request"));
        // without `on` it asks: the watch page's like button
        CHECK(parse(c.cmd("like", "\"id\":\"vid00001AAA\""), d)["result"]["on"].boolean());
        CHECK_EQ(f.last_url(), std::string("https://www.youtube.com/youtubei/v1/next?prettyPrint=false"));
        CHECK(!parse(c.cmd("like", "\"id\":\"vid00002BBB\""), d)["result"]["on"].boolean(true));
        CHECK_EQ(code(c.cmd("like", "\"id\":\"vid00001AAA\",\"on\":\"yes\"")), std::string("bad_request"));
    }
    e.stop();
    t.join();
    return 0;
}
