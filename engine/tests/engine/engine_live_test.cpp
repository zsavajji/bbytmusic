// The real Engine against YouTube (label `live`): search, play with radio, seek, next, stop and
// quit with a real-time WAV sink; then the WAV holds sound, and a second play of the same song,
// by a new engine over the same data folder, is served from the segment cache.
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "audio/wav_sink.h"
#include "check.h"
#include "engine/client.h"
#include "engine/engine.h"
#include "engine/paced_sink.h"

using namespace bbm;
using etest::Client;
using etest::is_ev;
using etest::ms_since;
using etest::parse;
using Clock = std::chrono::steady_clock;

namespace {

std::unique_ptr<Engine> make_engine(const std::string &dir) {
    EngineOptions o;
    o.data_dir = dir;
    o.version = "live";
    const std::string wav = dir + "/out.wav";
    o.sinks = [wav] { return std::make_unique<PacedSink>(std::make_unique<WavSink>(wav)); };
    auto e = std::make_unique<Engine>(std::move(o));
    std::string err;
    CHECK(e->start(err));
    return e;
}

void step(const char *what, Clock::time_point t0) { std::printf("  %-44s %6lld ms\n", what, (long long)ms_since(t0)); }

// Seconds of the WAV whose 10 ms windows are not silence (mean |sample| over 100).
double audible_s(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return 0;
    std::fseek(f, 44, SEEK_SET);
    std::vector<int16_t> win(480 * 2);
    int loud = 0;
    while (std::fread(win.data(), 2, win.size(), f) == win.size()) {
        int64_t sum = 0;
        for (int16_t v : win) sum += v < 0 ? -v : v;
        loud += sum / int64_t(win.size()) > 100;
    }
    std::fclose(f);
    return loud / 100.0;
}

}  // namespace

int main() {
    char tmpl[] = "/tmp/bbm_engine_live_XXXXXX";
    const std::string dir = mkdtemp(tmpl);
    JsonDoc d;
    std::string song;
    {
        auto e = make_engine(dir);
        std::thread run([&] { e->run(); });
        Client c(e->port());
        CHECK(c.hello(e->token()));

        auto t0 = Clock::now();
        Json r = parse(c.cmd("search", "\"q\":\"never gonna give you up\",\"filter\":\"songs\""), d)["result"];
        step("search songs", t0);
        Json first;
        for (Json sec : r["sections"].items())
            for (Json it : sec["items"].items())
                if (first.is_null() && it["kind"].str() == "song") first = it;
        CHECK(!first.is_null());
        song = first["id"].string();
        std::printf("  first song: %s \"%s\" by %s, %lld s\n", song.c_str(), first["title"].string().c_str(),
                    first["artist"].string().c_str(), (long long)first["duration_s"].i64());

        t0 = Clock::now();
        size_t mark = c.events.size();
        const std::string item = "{\"kind\":\"song\",\"id\":" + json_quote(song) + ",\"title\":" + json_quote(first["title"].str()) +
                                 ",\"duration_s\":" + std::to_string(first["duration_s"].i64()) + "}";
        CHECK(parse(c.cmd("play", "\"items\":[" + item + "],\"radio\":true"), d)["ok"].boolean());
        step("play with radio: reply (radio's first page in)", t0);
        CHECK(c.wait_event([](Json e) { return is_ev(e, "state") && e["state"].str() == "playing"; }, 30000, mark) >= 0);
        step("  state playing", t0);
        CHECK(c.wait_event([](Json e) { return is_ev(e, "position") && e["position_ms"].i64() > 3000; }, 30000, mark) >= 0);
        step("  position past 3 s", t0);
        Json q = parse(c.cmd("queue"), d)["result"];
        const size_t qlen = q["items"].size();
        std::printf("  queue after the radio: %zu tracks\n", qlen);
        CHECK(qlen > 5);

        t0 = Clock::now();
        mark = c.events.size();
        CHECK(parse(c.cmd("seek", "\"ms\":60000"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "position") && e["position_ms"].i64() >= 61000 && e["position_ms"].i64() < 70000; }, 30000, mark) >= 0);
        step("seek to 60 000: position past 61 s", t0);

        t0 = Clock::now();
        mark = c.events.size();
        CHECK(parse(c.cmd("next"), d)["ok"].boolean());
        const int tr = c.wait_event([](Json e) { return is_ev(e, "track") && e["index"].i64() == 1; }, 30000, mark);
        CHECK(tr >= 0);
        std::string next_id;
        if (tr >= 0) {
            JsonDoc t;
            next_id = parse(c.events[size_t(tr)], t)["track"]["id"].string();
            std::printf("  next: %s \"%s\"\n", next_id.c_str(), parse(c.events[size_t(tr)], t)["track"]["title"].string().c_str());
        }
        CHECK(!next_id.empty() && next_id != song);
        CHECK(c.wait_event([&](Json e) { return is_ev(e, "position") && e["position_ms"].i64() >= 1000; }, 30000, size_t(tr < 0 ? 0 : tr)) >= 0);
        step("next: a different track, position past 1 s", t0);
        Json s = parse(c.cmd("status"), d)["result"];
        CHECK_EQ(s["state"].string(), "playing");
        CHECK_EQ(s["track"]["id"].string(), next_id);

        t0 = Clock::now();
        mark = c.events.size();
        CHECK(parse(c.cmd("stop"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "state") && e["state"].str() == "stopped"; }, 10000, mark) >= 0);
        step("stop", t0);
        std::printf("  errors seen: %d\n", c.wait_event([](Json e) { return is_ev(e, "error"); }, 0) >= 0);
        CHECK(parse(c.cmd("quit"), d)["ok"].boolean());
        CHECK_EQ(c.line(5000), "<EOF>");
        run.join();
        std::printf("  media requests the cache could not serve: %d\n", e->media_fetches());
    }

    const double heard = audible_s(dir + "/out.wav");
    std::printf("  WAV: %.2f s of sound\n", heard);
    CHECK(heard > 3.0);

    // A second play of the same song, by a new engine, comes from the cache.
    {
        auto e = make_engine(dir);
        std::thread run([&] { e->run(); });
        Client c(e->port());
        CHECK(c.hello(e->token()));
        CHECK(parse(c.cmd("stop"), d)["ok"].boolean());   // the restored queue is not what we play
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const int before = e->media_fetches();
        auto t0 = Clock::now();
        const size_t mark = c.events.size();
        CHECK(parse(c.cmd("play", "\"items\":[{\"kind\":\"song\",\"id\":" + json_quote(song) + "}]"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "state") && e["state"].str() == "playing"; }, 30000, mark) >= 0);
        step("second play from the cache: playing", t0);
        CHECK(c.wait_event([](Json e) { return is_ev(e, "position") && e["position_ms"].i64() >= 2000; }, 30000, mark) >= 0);
        std::printf("  media requests during the second play: %d\n", e->media_fetches() - before);
        CHECK_EQ(e->media_fetches() - before, 0);
        CHECK(parse(c.cmd("quit"), d)["ok"].boolean());
        run.join();
    }

    // A podcast episode from Home's Podcasts chip plays like a video, alone: no radio follows it.
    {
        auto e = make_engine(dir);
        std::thread run([&] { e->run(); });
        Client c(e->port());
        CHECK(c.hello(e->token()));
        CHECK(parse(c.cmd("stop"), d)["ok"].boolean());
        auto t0 = Clock::now();
        Json s = parse(c.cmd("search", "\"q\":\"joe rogan\",\"filter\":\"podcasts\""), d)["result"];
        step("search podcasts", t0);
        size_t shows = 0, others = 0;
        for (Json sec : s["sections"].items())
            for (Json it : sec["items"].items()) {
                if (it["kind"].str() == "show" && it["id"].str().rfind("MPSP", 0) == 0) ++shows;
                else ++others;
            }
        std::printf("  podcasts search: %zu shows, %zu others\n", shows, others);
        CHECK(shows > 0);
        CHECK_EQ(others, size_t(0));
        t0 = Clock::now();
        Json r = parse(c.cmd("browse", "\"id\":\"FEmusic_home\",\"params\":\"ggNCSgQIDBADSgQICRABSgQIBxABSgQICBABSgQIBBABSgQIDhABSgQIAxABSgQIDRABSgQIChABSgQIBhABSgQIBRAB\""), d)["result"];
        step("browse the Podcasts chip", t0);
        std::string item;
        for (Json sec : r["sections"].items())
            for (Json it : sec["items"].items())
                if (item.empty() && it["kind"].str() == "episode")
                    item = "{\"kind\":\"episode\",\"id\":" + json_quote(it["id"].str()) + ",\"title\":" + json_quote(it["title"].str()) +
                           ",\"artist\":" + json_quote(it["artist"].str()) + ",\"album_id\":" + json_quote(it["album_id"].str()) +
                           ",\"duration_s\":" + std::to_string(it["duration_s"].i64()) + "}";
        CHECK(!item.empty());
        std::printf("  first episode: %s\n", item.c_str());
        t0 = Clock::now();
        const size_t mark = c.events.size();
        CHECK(parse(c.cmd("play", "\"items\":[" + item + "],\"radio\":true"), d)["ok"].boolean());   // radio is ignored
        CHECK(c.wait_event([](Json e) { return is_ev(e, "state") && e["state"].str() == "playing"; }, 30000, mark) >= 0);
        step("episode: playing", t0);
        CHECK(c.wait_event([](Json e) { return is_ev(e, "position") && e["position_ms"].i64() > 3000; }, 30000, mark) >= 0);
        step("  position past 3 s", t0);
        Json q = parse(c.cmd("queue"), d)["result"];
        CHECK_EQ(q["items"].size(), size_t(1));
        CHECK_EQ(q["items"][0]["kind"].string(), "episode");
        CHECK(parse(c.cmd("stop"), d)["ok"].boolean());
        CHECK(parse(c.cmd("quit"), d)["ok"].boolean());
        run.join();
        const double ep_heard = audible_s(dir + "/out.wav");
        std::printf("  WAV: %.2f s of sound\n", ep_heard);
        CHECK(ep_heard > 2.0);   // the position passed 3 s; a talk has quiet gaps
    }
    return check_done();
}
