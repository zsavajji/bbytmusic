// Account alone (a fake HTTP that routes by url), then through a real Engine and its socket; no network.
#include <sys/stat.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
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
    // the device-code flow
    std::atomic<int> polls{0}, revokes{0};
    std::atomic<int> pending_polls{2};   // "authorization_pending" this many times, then a token
    std::atomic<bool> deny{false};
    std::atomic<bool> hold{false}, held{false};   // while hold, the device/code request is stuck inside the fake
    HttpSendFn fn() {
        return [this](const HttpRequest &r) {
            HttpResponse o;
            o.status = 200;
            if (r.url == "https://www.youtube.com/o/oauth2/device/code" && hold) {
                held = true;
                while (hold && !(r.cancel && r.cancel->load())) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                held = false;
                o.status = 0;
                o.error = "cancelled";
                return o;
            }
            if (r.url == "https://www.youtube.com/o/oauth2/device/code") {
                o.body = "{\"device_code\":\"DEVCODE\",\"user_code\":\"ABCD-EFGH\",\"verification_url\":\"https://www.google.com/device\","
                         "\"interval\":0,\"expires_in\":60}";
            } else if (r.url == "https://oauth2.googleapis.com/revoke?token=OWN-REFRESH") {
                revokes++;
            } else if (r.url == "https://www.youtube.com/o/oauth2/token" && r.body.find("device/1.0") != std::string::npos) {
                polls++;
                if (deny) {
                    o.status = 400;
                    o.body = "{\"error\":\"access_denied\"}";
                } else if (pending_polls > 0) {
                    pending_polls--;
                    o.status = 428;
                    o.body = "{\"error\":\"authorization_pending\"}";
                } else {
                    o.body = "{\"access_token\":\"ACCESS-OWN\",\"refresh_token\":\"OWN-REFRESH\",\"expires_in\":3600}";
                }
            } else if (down) {
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

template <class F>
bool wait_until(F f, int ms = 5000) {
    for (int i = 0; i < ms / 5; i++) {
        if (f()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return f();
}

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

    // the sign-in, alone
    {
        Fake f;
        Account a(f.fn());
        std::mutex mu;
        std::vector<Account::Info> log;
        a.on_change = [&] {
            const Account::Info i = a.info();
            std::lock_guard<std::mutex> lk(mu);
            log.push_back(i);
        };
        a.sign_in();
        CHECK(a.info().pending);
        CHECK(wait_until([&] { return !a.info().pending; }));
        const Account::Info i = a.info();
        CHECK(i.in);
        CHECK_EQ(i.source, std::string("own"));
        CHECK_EQ(i.name, std::string("Test User"));
        CHECK_EQ(i.code, std::string(""));
        CHECK_EQ(i.error, std::string(""));
        CHECK_EQ(f.polls.load(), 3);
        CHECK(a.stored().find("OWN-REFRESH") != std::string::npos);
        CHECK(a.stored().find("FakeSecret") != std::string::npos);
        CHECK(a.stored().find("9-fake.apps.googleusercontent.com") != std::string::npos);
        {
            std::lock_guard<std::mutex> lk(mu);
            CHECK(log.size() >= 3);
            CHECK(log.front().pending && log.front().code.empty());
            bool coded = false;
            for (auto &e : log) coded |= e.pending && e.code == "ABCD-EFGH" && e.url == "https://www.google.com/device";
            CHECK(coded);
            CHECK(log.back().in && !log.back().pending);
        }
        // sign_out of our own account gives its token back; the refresh token is what is revoked
        CHECK_EQ(a.sign_out(), std::string("OWN-REFRESH"));
        CHECK(!a.info().in);
        a.revoke("OWN-REFRESH");
        CHECK_EQ(f.revokes.load(), 1);
        // and so does one that came from Tube: a sign-out here ends both
        CHECK(a.import("R", {"id", "sec"}, "tube", err) == OAuthResult::Ok);
        CHECK_EQ(a.sign_out(), std::string("R"));

        // refused
        f.deny = true;
        a.sign_in();
        CHECK(wait_until([&] { return !a.info().pending; }));
        CHECK(!a.info().in);
        CHECK_EQ(a.info().error, std::string("access_denied"));
        CHECK_EQ(a.info().code, std::string(""));
        a.sign_out();   // clears the error
        CHECK_EQ(a.info().error, std::string(""));
        f.deny = false;

        // cancelled while polling
        f.pending_polls = 1000000;
        a.sign_in();
        CHECK(wait_until([&] { return !a.info().code.empty(); }));
        a.cancel_sign_in();
        CHECK(!a.info().pending);
        CHECK(!a.info().in);
        CHECK_EQ(a.info().code, std::string(""));
        CHECK_EQ(a.info().error, std::string(""));
        const size_t n = [&] { std::lock_guard<std::mutex> lk(mu); return log.size(); }();
        a.cancel_sign_in();   // none in flight: nothing
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK_EQ(([&] { std::lock_guard<std::mutex> lk(mu); return log.size(); })(), n);
        const int polls = f.polls;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK_EQ(f.polls.load(), polls);   // the thread is gone
    }
    // info() does not wait for a request of the sign-in thread; a cancel cuts the request short
    {
        Fake f;
        f.hold = true;
        Account a(f.fn());
        a.sign_in();
        CHECK(wait_until([&] { return f.held.load(); }));
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(a.info().pending);
        CHECK_EQ(a.stored(), std::string(""));
        CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(200));
        a.cancel_sign_in();   // f.hold is still set: the cancel flag frees the request
        CHECK(!a.info().pending);
        CHECK(!f.held.load());
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
        Json ex = parse(c.cmd("account", "\"export\":true"), d)["result"];
        CHECK_EQ(ex["state"].string(), "in");
        CHECK_EQ(ex["refresh_token"].string(), "REFRESH-1");
        CHECK_EQ(ex["client_id"].string(), "9-fake.apps.googleusercontent.com");
        CHECK_EQ(ex["client_secret"].string(), "FakeSecret");
        CHECK_EQ(parse(c.cmd("account", "\"export\":true,\"sign_out\":true"), d)["error"]["code"].string(), "bad_request");
    }
    {
        Running r(dir, f.fn());
        Client c(r.e->port());
        CHECK(c.hello(r.e->token()));
        Json res = parse(c.cmd("account"), d)["result"];
        CHECK_EQ(res["state"].string(), "in");
        CHECK_EQ(res["name"].string(), "Test User");
        CHECK_EQ(parse(c.cmd("account", "\"sign_out\":true"), d)["result"]["state"].string(), "out");
        CHECK_EQ(parse(c.cmd("account", "\"export\":true"), d)["error"]["code"].string(), "signed_out");
        CHECK(c.wait_event([](Json e) { return is_ev(e, "account") && e["state"].str() == "out"; }, 3000) >= 0);
    }
    {
        Running r(dir, f.fn());
        Client c(r.e->port());
        CHECK(c.hello(r.e->token()));
        CHECK_EQ(parse(c.cmd("account"), d)["result"]["state"].string(), "out");
    }
    {   // the sign-in
        char tm2[] = "/tmp/bbm_account_XXXXXX";
        const std::string dir2 = mkdtemp(tm2);
        Fake g;
        Running r(dir2, g.fn());
        Client c(r.e->port());
        CHECK(c.hello(r.e->token()));
        Json res = parse(c.cmd("account", "\"sign_in\":true"), d)["result"];
        CHECK_EQ(res["state"].string(), "pending");
        CHECK_EQ(res["code"].string(), "");
        CHECK(c.wait_event([](Json e) { return is_ev(e, "account") && e["state"].str() == "pending" && e["code"].str() == "ABCD-EFGH"; }, 3000) >= 0);
        CHECK(c.wait_event([](Json e) { return is_ev(e, "account") && e["state"].str() == "in" && e["source"].str() == "own"; }, 3000) >= 0);
        CHECK_EQ(parse(c.cmd("account", "\"sign_in\":true,\"sign_out\":true"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("account", "\"cancel\":true,\"sign_in\":true"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("account", "\"sign_out\":true"), d)["result"]["state"].string(), "out");
        CHECK(wait_until([&] { return g.revokes.load() == 1; }));
    }
    return check_done();
}
