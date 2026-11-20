// The favourites through a real Engine and its socket, with no network: the downloader reads a slow
// fake source, so shutdown must cut it short.
#include <sys/stat.h>

#include <cstdlib>
#include <memory>
#include <thread>

#include "audio/null_sink.h"
#include "check.h"
#include "engine/client.h"
#include "engine/engine.h"
#include "player/support.h"

using namespace bbm;
using etest::Client;
using etest::is_ev;
using etest::parse;

namespace {

std::string g_dir;

// A started engine on g_dir, run on a thread of its own. Every source takes 2 s per segment.
struct Running {
    std::unique_ptr<Engine> e;
    std::thread t;
    Running() {
        EngineOptions o;
        o.data_dir = g_dir;
        o.version = "test";
        o.sinks = [] { return std::make_unique<NullSink>(true); };
        o.sources = [](const Track &, std::string &err) -> std::unique_ptr<Source> {
            ptest::FileSource::Options so;
            so.delay_ms = 2000;
            so.loops = 5;
            auto s = std::make_unique<ptest::FileSource>(ptest::fixture("tone440_12s.webm"), so);
            if (!s->open(err)) return nullptr;
            return s;
        };
        e = std::make_unique<Engine>(std::move(o));
        std::string err;
        CHECK(e->start(err));
        t = std::thread([this] { e->run(); });
    }
    ~Running() {
        e->stop();
        if (t.joinable()) t.join();
    }
};

std::string item(const std::string &id, const char *kind = "song") {
    return "{\"kind\":\"" + std::string(kind) + "\",\"id\":\"" + id + "\",\"title\":\"T " + id +
           "\",\"artist\":\"Synth\",\"album\":\"Tones\",\"duration_s\":12}";
}

std::string ids(Json items) {
    std::string s;
    for (Json i : items.items()) s += i["id"].string() + ",";
    return s;
}

}  // namespace

int main() {
    char tmpl[] = "/tmp/bbm_fav_engine_XXXXXX";
    g_dir = mkdtemp(tmpl);
    JsonDoc d;
    const auto t0 = std::chrono::steady_clock::now();

    {
        Running r;
        Client c(r.e->port());
        CHECK(c.hello(r.e->token()));
        CHECK(parse(c.cmd("favorites"), d)["result"]["items"].size() == 0);
        CHECK_EQ(parse(c.cmd("favorite", "\"id\":\"s1\""), d)["result"]["on"].boolean(), false);

        Json a = parse(c.cmd("favorite", "\"item\":" + item("s1") + ",\"on\":true"), d)["result"];
        CHECK_EQ(a["id"].string(), "s1");
        CHECK(a["on"].boolean());
        const int ev = c.wait_event([](Json e) { return is_ev(e, "favorite") && e["count"].i64() == 1; }, 5000);
        CHECK(ev >= 0);
        if (ev >= 0) {
            JsonDoc ed;
            Json e = parse(c.events[size_t(ev)], ed);
            CHECK_EQ(e["id"].string(), "s1");
            CHECK(e["on"].boolean());
            CHECK(!e["offline"].boolean());
        }
        CHECK(parse(c.cmd("favorite", "\"id\":\"s1\""), d)["result"]["on"].boolean());

        // twice: still a success, and an event
        const size_t mark = c.events.size();
        CHECK(parse(c.cmd("favorite", "\"item\":" + item("s1") + ",\"on\":true"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "favorite") && e["count"].i64() == 1; }, 5000, mark) >= 0);

        CHECK(parse(c.cmd("favorite", "\"item\":" + item("s2") + ",\"on\":true"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "favorite") && e["count"].i64() == 2; }, 5000) >= 0);
        Json l = parse(c.cmd("favorites"), d)["result"]["items"];
        CHECK_EQ(ids(l), "s2,s1,");
        CHECK_EQ(l[1]["title"].string(), "T s1");
        CHECK_EQ(l[1]["kind"].string(), "song");
        CHECK(l[1]["added_at"].i64() > 0);
        CHECK(l[1]["offline"].is_bool());
        CHECK(!l[1]["offline"].boolean());

        // not playable, or no item to add
        CHECK_EQ(parse(c.cmd("favorite", "\"item\":" + item("MPREb_1", "album") + ",\"on\":true"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("favorite", "\"item\":\"x\",\"on\":true"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("favorite", "\"id\":\"s3\",\"on\":true"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("favorite"), d)["error"]["code"].string(), "bad_request");
    }

    {  // restarted on the same folder: the list is still there (and the downloader is cut short at the end)
        Running r;
        Client c(r.e->port());
        CHECK(c.hello(r.e->token()));
        CHECK_EQ(ids(parse(c.cmd("favorites"), d)["result"]["items"]), "s2,s1,");
        CHECK(parse(c.cmd("favorite", "\"id\":\"s2\""), d)["result"]["on"].boolean());

        const size_t mark = c.events.size();
        Json off = parse(c.cmd("favorite", "\"id\":\"s2\",\"on\":false"), d)["result"];
        CHECK(!off["on"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "favorite") && !e["on"].boolean() && e["count"].i64() == 1; }, 5000, mark) >= 0);
        CHECK(!parse(c.cmd("favorite", "\"id\":\"s2\""), d)["result"]["on"].boolean());
        CHECK_EQ(ids(parse(c.cmd("favorites"), d)["result"]["items"]), "s1,");

        // removing what is not there succeeds and still emits
        const size_t mark2 = c.events.size();
        CHECK(parse(c.cmd("favorite", "\"id\":\"s2\",\"on\":false"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "favorite") && e["count"].i64() == 1; }, 5000, mark2) >= 0);

        CHECK(parse(c.cmd("favorite", "\"id\":\"s1\",\"on\":false"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "favorite") && e["count"].i64() == 0; }, 5000, mark2) >= 0);
        CHECK(parse(c.cmd("favorites"), d)["result"]["items"].size() == 0);
        CHECK(parse(c.cmd("favorite", "\"item\":" + item("s1") + ",\"on\":true"), d)["ok"].boolean());
    }   // the downloader is in a 2 s read: the destructor cancels it

    {
        Running r;
        Client c(r.e->port());
        CHECK(c.hello(r.e->token()));
        CHECK_EQ(ids(parse(c.cmd("favorites"), d)["result"]["items"]), "s1,");
    }
    CHECK(etest::ms_since(t0) < 20000);
    return check_done();
}
