// Account alone (a fake HTTP that routes by url), then through a real Engine and its socket; no network.
#include <sys/stat.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <thread>

#include "audio/null_sink.h"
#include "check.h"
#include "engine/account.h"
#include "engine/client.h"
#include "engine/engine.h"
#include "player/support.h"

using namespace bbm;
using etest::Client;
using etest::is_ev;
using etest::parse;

namespace {

// The service: the TV page, its script, the token endpoint and the account list.
struct Fake {
    std::atomic<int> tv{0}, js{0}, token{0}, accounts{0};
    std::atomic<int> expires_in{3600};
    std::atomic<bool> grant_ok{true}, down{false};
    std::string name = "Test User";
    std::string new_refresh;   // when set, the token answer carries it
    std::string last_auth;
    HttpSendFn fn() {
        return [this](const HttpRequest &r) {
            HttpResponse o;
            o.status = 200;
            if (down) {
                o.status = 0;
                o.error = "unreachable";
            } else if (r.url == "https://www.youtube.com/tv") {
                tv++;
                o.body = "<script id=\"base-js\" src=\"/s/tv/m=base\"></script>";
            } else if (r.url == "https://www.youtube.com/s/tv/m=base") {
                js++;
                o.body = "x clientId:\"9-fake.apps.googleusercontent.com\",abc:\"FakeSecret\" y";
            } else if (r.url == "https://www.youtube.com/o/oauth2/token") {
                token++;
                if (!grant_ok) {
                    o.status = 400;
                    o.body = "{\"error\":\"invalid_grant\"}";
                } else {
                    o.body = "{\"access_token\":\"ACCESS-1\",\"expires_in\":" + std::to_string(expires_in.load()) +
                             (new_refresh.empty() ? "" : ",\"refresh_token\":\"" + new_refresh + "\"") + "}";
                }
            } else if (r.url.find("accounts_list") != std::string::npos) {
                accounts++;
                o.body = "{\"contents\":[{\"accountSectionListRenderer\":{\"contents\":[{\"accountItemSectionRenderer\":{\"contents\":["
                         "{\"accountItem\":{\"accountName\":{\"simpleText\":\"" + name +
                         "\"},\"accountPhoto\":{\"thumbnails\":[{\"url\":\"https://yt3.ggpht.com/p\"}]},\"isSelected\":true}}]}}]}}]}";
            } else {
                o.status = 404;
            }
            return o;
        };
    }
};

std::unique_ptr<Engine> make_engine(const std::string &dir, HttpSendFn http) {
    EngineOptions o;
    o.data_dir = dir;
    o.version = "test";
    o.sinks = [] { return std::make_unique<NullSink>(true); };
    o.account_http = std::move(http);
    return std::make_unique<Engine>(std::move(o));
}

struct Running {
    std::unique_ptr<Engine> e;
    std::thread t;
    Running(const std::string &dir, HttpSendFn http) : e(make_engine(dir, std::move(http))) {
        std::string err;
        CHECK(e->start(err));
        t = std::thread([this] { e->run(); });
    }
    ~Running() {
        e->stop();
        if (t.joinable()) t.join();
    }
};

}  // namespace

int main() {
    std::string err;
    {
        Fake f;
        Account a(f.fn());
        int changes = 0;
        a.on_change = [&] { changes++; };
        CHECK(!a.info().in);
        CHECK_EQ(a.stored(), std::string(""));

        // no pair: the TV page is read
        CHECK(a.import("REFRESH-1", {}, "tube", err) == OAuthResult::Ok);
        CHECK_EQ(f.tv.load(), 1);
        CHECK_EQ(f.js.load(), 1);
        CHECK_EQ(changes, 1);
        Account::Info i = a.info();
        CHECK(i.in);
        CHECK_EQ(i.name, std::string("Test User"));
        CHECK_EQ(i.photo, std::string("https://yt3.ggpht.com/p"));
        CHECK_EQ(i.source, std::string("tube"));
        CHECK(a.stored().find("ACCESS-1") == std::string::npos);
        CHECK(a.stored().find("FakeSecret") != std::string::npos);

        // the stored form comes back
        Account b(f.fn());
        b.load(a.stored());
        CHECK(b.info().in);
        CHECK_EQ(b.info().name, std::string("Test User"));
        CHECK_EQ(b.stored(), a.stored());
        Account g(f.fn());
        g.load("garbage");
        CHECK(!g.info().in);
        g.load("");
        CHECK(!g.info().in);

        // bearer: no second refresh while the token has time; load() holds none
        std::string tok;
        CHECK(a.bearer(tok, err) == OAuthResult::Ok);
        CHECK_EQ(tok, std::string("ACCESS-1"));
        CHECK_EQ(f.token.load(), 1);
        CHECK(b.bearer(tok, err) == OAuthResult::Ok);
        CHECK_EQ(f.token.load(), 2);

        // a new refresh token is kept
        f.expires_in = 0;   // the token the import holds is already out of time
        CHECK(a.import("REFRESH-1", {"id", "sec"}, "tube", err) == OAuthResult::Ok);
        f.new_refresh = "REFRESH-2";
        const int before = changes;
        const int t0 = f.token;
        CHECK(a.bearer(tok, err) == OAuthResult::Ok);
        CHECK_EQ(f.token.load(), t0 + 1);
        CHECK_EQ(changes, before + 1);
        CHECK(a.stored().find("REFRESH-2") != std::string::npos);
        f.new_refresh.clear();
        f.expires_in = 3600;

        // a transport error: Network, still signed in
        f.down = true;
        CHECK(a.bearer(tok, err) == OAuthResult::Network);
        CHECK(a.info().in);
        f.down = false;

        // a failed import keeps the account
        {
            CHECK(a.import("X", {"id", "sec"}, "tube", err) == OAuthResult::Ok);   // the working fake
            const std::string kept = a.stored();
            f.grant_ok = false;
            CHECK(a.import("X", {"id", "sec"}, "tube", err) == OAuthResult::Refused);
            CHECK_EQ(err, std::string("invalid_grant"));
            CHECK_EQ(a.stored(), kept);
            f.down = true;
            CHECK(a.import("X", {}, "tube", err) == OAuthResult::Network);
            CHECK_EQ(a.stored(), kept);
            f.down = false;
            f.grant_ok = true;
        }
        // with a pair, the TV page is not read
        const int tv = f.tv;
        CHECK(a.import("R", {"id", "sec"}, "tube", err) == OAuthResult::Ok);
        CHECK_EQ(f.tv.load(), tv);
        f.expires_in = 0;   // so that the next bearer() refreshes
        CHECK(a.import("R", {"id", "sec"}, "tube", err) == OAuthResult::Ok);

        // a refused grant signs out
        f.grant_ok = false;
        const int c2 = changes;
        CHECK(a.bearer(tok, err) == OAuthResult::Refused);
        CHECK(!a.info().in);
        CHECK_EQ(changes, c2 + 1);
        CHECK_EQ(a.stored(), std::string(""));

        // the name fails on its own: the import still succeeds
        f.grant_ok = true;
        f.name = "";
        CHECK(a.import("R", {"id", "sec"}, "tube", err) == OAuthResult::Ok);
        f.name = "Test User";

        // sign_out, once
        const int c3 = changes;
        a.sign_out();
        CHECK(!a.info().in);
        CHECK_EQ(changes, c3 + 1);
        a.sign_out();
        CHECK_EQ(changes, c3 + 1);
    }

    // through the engine
    char tmpl[] = "/tmp/bbm_account_XXXXXX";
    const std::string dir = mkdtemp(tmpl);
    JsonDoc d;
    Fake f;
    const std::string imp = "\"import\":{\"refresh_token\":\"REFRESH-1\",\"access_token\":\"ignored\",\"v\":1}";
    {
        Running r(dir, f.fn());
        Client c(r.e->port());
        CHECK(c.hello(r.e->token()));
        CHECK_EQ(parse(c.cmd("account"), d)["result"]["state"].string(), "out");
        for (const char *bad : {"\"import\":5", "\"import\":{}", "\"import\":{\"refresh_token\":\"\"}",
                                "\"import\":{\"refresh_token\":\"R\",\"client_id\":\"a\"}",
                                "\"import\":{\"refresh_token\":\"R\",\"client_id\":\"a\",\"client_secret\":\"\"}",
                                "\"import\":{\"refresh_token\":\"R\"},\"sign_out\":true"})
            CHECK_EQ(parse(c.cmd("account", bad), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(f.token.load(), 0);

        f.grant_ok = false;
        CHECK_EQ(parse(c.cmd("account", imp), d)["error"]["code"].string(), "refused");
        CHECK_EQ(parse(c.cmd("account"), d)["result"]["state"].string(), "out");
        f.down = true;
        CHECK_EQ(parse(c.cmd("account", imp), d)["error"]["code"].string(), "network");
        f.down = false;
        f.grant_ok = true;

        const std::string line = c.cmd("account", imp);
        Json res = parse(line, d)["result"];
        CHECK_EQ(res["state"].string(), "in");
        CHECK_EQ(res["name"].string(), "Test User");
        CHECK_EQ(res["source"].string(), "tube");
        CHECK(line.find("REFRESH-1") == std::string::npos);
        CHECK(c.wait_event([](Json e) { return is_ev(e, "account") && e["state"].str() == "in" && e["name"].str() == "Test User"; }, 3000) >= 0);
    }
    {
        Running r(dir, f.fn());
        Client c(r.e->port());
        CHECK(c.hello(r.e->token()));
        Json res = parse(c.cmd("account"), d)["result"];
        CHECK_EQ(res["state"].string(), "in");
        CHECK_EQ(res["name"].string(), "Test User");
        CHECK_EQ(parse(c.cmd("account", "\"sign_out\":true"), d)["result"]["state"].string(), "out");
        CHECK(c.wait_event([](Json e) { return is_ev(e, "account") && e["state"].str() == "out"; }, 3000) >= 0);
    }
    {
        Running r(dir, f.fn());
        Client c(r.e->port());
        CHECK(c.hello(r.e->token()));
        CHECK_EQ(parse(c.cmd("account"), d)["result"]["state"].string(), "out");
    }
    return check_done();
}
