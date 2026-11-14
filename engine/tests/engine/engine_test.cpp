// The Engine without the network: the player tests' tone files as the sources, a real-time null
// sink, a temp data folder, driven through its real socket.
#include <sys/stat.h>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>

#include "audio/null_sink.h"
#include "check.h"
#include "engine/client.h"
#include "engine/engine.h"
#include "player/support.h"
#include "store/history_store.h"
#include "store/schema.h"

using namespace bbm;
using etest::Client;
using etest::is_ev;
using etest::parse;

namespace {

std::string g_dir;

// Fails with "unplugged" once, after `left` frames, as QsaSink does when the headphones come out.
struct UnplugOnce : Sink {
    std::shared_ptr<std::atomic<int>> left;
    NullSink n{true};
    bool gone = false;
    explicit UnplugOnce(std::shared_ptr<std::atomic<int>> l) : left(std::move(l)) {}
    bool open(int rate, int ch, std::string &err) override {
        gone = false;
        return n.open(rate, ch, err);
    }
    bool write(const int16_t *p, size_t frames, std::string &err) override {
        if (gone || (*left > 0 && (*left -= int(frames)) <= 0)) {
            gone = true;
            err = "unplugged";
            return false;
        }
        return n.write(p, frames, err);
    }
    void close() override { n.close(); }
};

std::unique_ptr<Engine> make_engine(int64_t idle_ms = 600000, SinkFactory sinks = {}) {
    EngineOptions o;
    o.data_dir = g_dir;
    o.version = "test";
    o.idle_exit_ms = idle_ms;
    o.sinks = sinks ? sinks : [] { return std::make_unique<NullSink>(true); };
    o.sources = [](const Track &t, std::string &err) -> std::unique_ptr<Source> {
        auto s = std::make_unique<ptest::FileSource>(ptest::fixture(t.id == "b" ? "tone880_8s.webm" : "tone440_12s.webm"));
        if (!s->open(err)) return nullptr;
        return s;
    };
    return std::make_unique<Engine>(std::move(o));
}

// A started engine, run on a thread of its own.
struct Running {
    std::unique_ptr<Engine> e;
    std::thread t;
    explicit Running(int64_t idle_ms = 600000, SinkFactory sinks = {}) : e(make_engine(idle_ms, std::move(sinks))) {
        std::string err;
        CHECK(e->start(err));
        CHECK_EQ(err, "");
        t = std::thread([this] { e->run(); });
    }
    ~Running() {
        e->stop();
        if (t.joinable()) t.join();
    }
};

bool exists(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

const char *kTwo =
    "\"items\":[{\"kind\":\"song\",\"id\":\"a\",\"title\":\"Tone A\",\"artist\":\"Synth\",\"album\":\"Tones\",\"duration_s\":12,"
    "\"explicit\":true},{\"kind\":\"video\",\"id\":\"b\",\"title\":\"Tone B\",\"artist\":\"Synth\",\"duration_s\":8}]";

std::string ids(Json items) {
    std::string s;
    for (Json i : items.items()) s += i["id"].string();
    return s;
}

}  // namespace

int main() {
    char tmpl[] = "/tmp/bbm_engine_XXXXXX";
    g_dir = mkdtemp(tmpl);
    JsonDoc d;

    {
        Running r;
        Client c(r.e->port());
        CHECK(c.hello(r.e->token()));
        Json s = parse(c.cmd("status"), d)["result"];
        CHECK_EQ(s["state"].string(), "stopped");
        CHECK_EQ(s["version"].string(), "test");
        CHECK(s["track"].is_null());
        CHECK_EQ(s["queue_len"].i64(), 0);

        // Refused: kinds that cannot be played, missing fields.
        CHECK_EQ(parse(c.cmd("play", "\"items\":[{\"kind\":\"album\",\"id\":\"MPREb_1\"}]"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("queue_add", "\"items\":[{\"kind\":\"artist\",\"id\":\"UC1\"}]"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("play", "\"items\":[]"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("play", std::string(kTwo) + ",\"index\":5"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("search"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("search", "\"q\":\"x\",\"filter\":\"podcasts\""), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("seek"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("jump", "\"index\":0"), d)["error"]["code"].string(), "not_found");

        // play: the events in order: queue, loading, track (then a position), playing.
        const size_t from = c.events.size();
        CHECK(parse(c.cmd("play", kTwo), d)["ok"].boolean());
        const int playing = c.wait_event([](Json e) { return is_ev(e, "state") && e["state"].str() == "playing"; }, 15000, from);
        CHECK(playing >= 0);
        const int queue = c.wait_event([](Json e) { return is_ev(e, "queue"); }, 0, from);
        const int loading = c.wait_event([](Json e) { return is_ev(e, "state") && e["state"].str() == "loading"; }, 0, from);
        const int track = c.wait_event([](Json e) { return is_ev(e, "track"); }, 0, from);
        CHECK(queue >= 0 && loading >= 0 && track >= 0);
        CHECK(queue < loading && loading < track && track < playing);
        CHECK(c.wait_event([](Json e) { return is_ev(e, "position"); }, 0, size_t(track)) == track + 1);
        {
            JsonDoc t;
            Json ev = parse(c.events[size_t(track)], t);
            CHECK_EQ(ev["index"].i64(), 0);
            CHECK_EQ(ev["track"]["id"].string(), "a");
            CHECK_EQ(ev["track"]["title"].string(), "Tone A");
            CHECK_EQ(ev["track"]["artist"].string(), "Synth");
            CHECK(ev["track"]["explicit"].boolean());
            CHECK_EQ(ev["track"]["duration_s"].i64(), 12);
        }
        CHECK(c.wait_event([](Json e) { return is_ev(e, "position") && e["position_ms"].i64() >= 1000; }, 5000, size_t(playing)) >= 0);

        Json q = parse(c.cmd("queue"), d)["result"];
        CHECK_EQ(ids(q["items"]), "ab");
        CHECK_EQ(q["index"].i64(), 0);
        CHECK_EQ(q["items"][1]["kind"].string(), "video");

        // queue_add, queue_move, queue_remove: each followed by a queue event.
        size_t mark = c.events.size();
        CHECK(parse(c.cmd("queue_add", "\"items\":[{\"kind\":\"song\",\"id\":\"c\",\"title\":\"Tone C\"}]"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "queue") && e["len"].i64() == 3; }, 5000, mark) >= 0);
        CHECK_EQ(ids(parse(c.cmd("queue"), d)["result"]["items"]), "abc");
        mark = c.events.size();
        CHECK(parse(c.cmd("queue_move", "\"from\":2,\"to\":0"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "queue") && e["index"].i64() == 1; }, 5000, mark) >= 0);
        q = parse(c.cmd("queue"), d)["result"];
        CHECK_EQ(ids(q["items"]), "cab");   // `to` is the final index; the current track keeps playing
        CHECK_EQ(q["index"].i64(), 1);
        CHECK_EQ(parse(c.cmd("queue_move", "\"from\":0,\"to\":3"), d)["error"]["code"].string(), "not_found");
        mark = c.events.size();
        CHECK(parse(c.cmd("queue_remove", "\"index\":0"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "queue") && e["len"].i64() == 2; }, 5000, mark) >= 0);
        q = parse(c.cmd("queue"), d)["result"];
        CHECK_EQ(ids(q["items"]), "ab");
        CHECK_EQ(q["index"].i64(), 0);
        CHECK_EQ(parse(c.cmd("queue_remove", "\"index\":2"), d)["error"]["code"].string(), "not_found");

        // queue_sort, queue_shuffle, queue_clear: each followed by a queue event; `a` plays on.
        CHECK(parse(c.cmd("queue_add", "\"items\":[{\"kind\":\"song\",\"id\":\"c\",\"title\":\"tone C\"}]"), d)["ok"].boolean());
        mark = c.events.size();
        CHECK(parse(c.cmd("queue_sort", "\"by\":\"title\",\"desc\":true"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "queue") && e["index"].i64() == 2; }, 5000, mark) >= 0);
        q = parse(c.cmd("queue"), d)["result"];
        CHECK_EQ(ids(q["items"]), "cba");
        CHECK_EQ(q["index"].i64(), 2);
        CHECK_EQ(parse(c.cmd("queue_sort", "\"by\":\"size\""), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("queue_sort"), d)["error"]["code"].string(), "bad_request");
        mark = c.events.size();
        CHECK(parse(c.cmd("queue_sort", "\"by\":\"title\""), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "queue") && e["index"].i64() == 0; }, 5000, mark) >= 0);
        CHECK_EQ(ids(parse(c.cmd("queue"), d)["result"]["items"]), "abc");
        mark = c.events.size();
        CHECK(parse(c.cmd("queue_shuffle"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "queue"); }, 5000, mark) >= 0);
        q = parse(c.cmd("queue"), d)["result"];
        CHECK_EQ(q["items"][0]["id"].string(), "a");
        CHECK_EQ(q["items"].size(), size_t(3));
        mark = c.events.size();
        CHECK(parse(c.cmd("queue_clear"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "queue") && e["len"].i64() == 1; }, 5000, mark) >= 0);
        q = parse(c.cmd("queue"), d)["result"];
        CHECK_EQ(ids(q["items"]), "a");
        CHECK_EQ(q["index"].i64(), 0);
        CHECK_EQ(parse(c.cmd("status"), d)["result"]["state"].string(), "playing");
        CHECK(parse(c.cmd("queue_add", "\"items\":[{\"kind\":\"video\",\"id\":\"b\",\"title\":\"Tone B\",\"duration_s\":8}]"), d)["ok"].boolean());
        CHECK_EQ(parse(c.cmd("browse"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("browse", "\"id\":\"\""), d)["error"]["code"].string(), "bad_request");

        CHECK_EQ(parse(c.cmd("thumbs"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("thumbs", "\"urls\":[1]"), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("thumbs", "\"urls\":[\"ftp://x/y\"]"), d)["result"]["files"].size(), size_t(0));
        CHECK_EQ(parse(c.cmd("repeat", "\"mode\":\"twice\""), d)["error"]["code"].string(), "bad_request");
        CHECK_EQ(parse(c.cmd("status"), d)["result"]["repeat"].string(), "off");
        mark = c.events.size();
        CHECK(parse(c.cmd("repeat", "\"mode\":\"all\""), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "mode") && e["repeat"].str() == "all"; }, 5000, mark) >= 0);
        CHECK_EQ(parse(c.cmd("status"), d)["result"]["repeat"].string(), "all");

        // seek: the position follows.
        mark = c.events.size();
        CHECK(parse(c.cmd("seek", "\"ms\":6000"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "position") && e["position_ms"].i64() >= 6000 && e["position_ms"].i64() < 9000; }, 5000, mark) >= 0);
        CHECK(c.wait_event([](Json e) { return is_ev(e, "position") && e["position_ms"].i64() >= 7000; }, 5000, mark) >= 0);
        s = parse(c.cmd("status"), d)["result"];
        CHECK(s["position_ms"].i64() >= 6000);
        CHECK_EQ(s["duration_ms"].i64() / 1000, 12);

        // next: the second track plays.
        mark = c.events.size();
        CHECK(parse(c.cmd("next"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "track") && e["index"].i64() == 1 && e["track"]["id"].str() == "b"; }, 5000, mark) >= 0);
        CHECK(c.wait_event([](Json e) { return is_ev(e, "state") && e["state"].str() == "playing" && e["track_id"].str() == "b"; }, 5000, mark) >= 0);
        s = parse(c.cmd("status"), d)["result"];
        CHECK_EQ(s["index"].i64(), 1);
        CHECK_EQ(s["track"]["id"].string(), "b");
        CHECK(c.wait_event([](Json e) { return is_ev(e, "position") && e["position_ms"].i64() >= 2000; }, 5000, mark) >= 0);

        // stop: stopped, the last track still in status.
        mark = c.events.size();
        CHECK(parse(c.cmd("stop"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "state") && e["state"].str() == "stopped"; }, 5000, mark) >= 0);
        s = parse(c.cmd("status"), d)["result"];
        CHECK_EQ(s["state"].string(), "stopped");
        CHECK_EQ(s["track"]["id"].string(), "b");

        // resume after a stop plays the current track again; pause keeps the position.
        mark = c.events.size();
        CHECK(parse(c.cmd("resume"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "position") && e["position_ms"].i64() >= 2000; }, 5000, mark) >= 0);
        mark = c.events.size();
        CHECK(parse(c.cmd("pause"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "state") && e["state"].str() == "paused"; }, 5000, mark) >= 0);

        CHECK(parse(c.cmd("quit"), d)["ok"].boolean());
        CHECK_EQ(c.line(3000), "<EOF>");   // the engine closes once quit is answered
        r.t.join();
        CHECK(!exists(g_dir + "/engine.json"));
    }

    // A restart restores the queue and the position, paused; status and queue answer at once.
    {
        Running r;
        Client c(r.e->port());
        CHECK(c.hello(r.e->token()));
        Json s = parse(c.cmd("status"), d)["result"];
        CHECK_EQ(s["state"].string(), "paused");
        CHECK_EQ(s["index"].i64(), 1);
        CHECK_EQ(s["queue_len"].i64(), 2);
        CHECK_EQ(s["track"]["id"].string(), "b");
        CHECK_EQ(s["track"]["title"].string(), "Tone B");
        CHECK_EQ(s["repeat"].string(), "all");   // kept across the restart
        CHECK(s["position_ms"].i64() >= 2000);
        Json q = parse(c.cmd("queue"), d)["result"];
        CHECK_EQ(ids(q["items"]), "ab");
        CHECK_EQ(q["items"][0]["album"].string(), "Tones");
        CHECK_EQ(q["index"].i64(), 1);
        const size_t mark = c.events.size();
        CHECK(parse(c.cmd("toggle"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "state") && e["state"].str() == "playing"; }, 5000, mark) >= 0);
        CHECK(c.wait_event([](Json e) { return is_ev(e, "position") && e["position_ms"].i64() >= 3000; }, 5000, mark) >= 0);
        const size_t stopped = c.events.size();
        CHECK(parse(c.cmd("stop"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "state") && e["state"].str() == "stopped"; }, 5000, stopped) >= 0);

        // history: the track just heard, first, once.
        Json h = parse(c.cmd("history"), d)["result"];
        CHECK_EQ(h["items"][0]["id"].string(), "b");
        CHECK_EQ(h["items"][0]["title"].string(), "Tone B");
        CHECK(h["items"][0]["played_at"].i64() > 0);
        CHECK_EQ(h["items"].size(), size_t(2));   // b and a, each once
        h = parse(c.cmd("history", "\"limit\":1"), d)["result"];
        CHECK_EQ(h["items"].size(), size_t(1));
        // a limit that is no number is the default; a negative one is the least, 1
        CHECK_EQ(parse(c.cmd("history", "\"limit\":\"x\""), d)["result"]["items"].size(), size_t(2));
        CHECK_EQ(parse(c.cmd("history", "\"limit\":-5"), d)["result"]["items"].size(), size_t(1));
    }

    // The history: a row per track left, with what was heard of it.
    {
        Db db;
        std::string err;
        std::vector<HistoryRow> rows;
        CHECK(open_store(db, g_dir + "/music.db", err));
        CHECK(recent_history(db, 10, rows, err));
        int a = 0, b = 0;
        for (const HistoryRow &h : rows) {
            CHECK(h.ms_played > 0);
            a += h.track_id == "a";
            b += h.track_id == "b";
            if (h.track_id == "a") CHECK(h.ms_played >= 1000 && h.ms_played < 6000);   // the seek is not counted
        }
        CHECK_EQ(a, 1);
        CHECK(b >= 2);
    }

    // The idle exit: nothing playing (the restored queue is paused) and no client.
    {
        auto e = make_engine(1000);
        std::string err;
        CHECK(e->start(err));
        const auto t0 = std::chrono::steady_clock::now();
        std::thread t([&] { e->run(); });
        {
            Client c(e->port());   // a client holds it open
            CHECK(c.hello(e->token()));
            std::this_thread::sleep_for(std::chrono::milliseconds(1800));
            CHECK(exists(g_dir + "/engine.json"));
        }
        t.join();
        const int64_t took = etest::ms_since(t0);
        CHECK(took >= 2500 && took < 6000);
        CHECK(!exists(g_dir + "/engine.json"));
    }

    // Headphones out pauses (not stops); resume plays on.
    {
        char t2[] = "/tmp/bbm_engine_XXXXXX";
        g_dir = mkdtemp(t2);
        auto left = std::make_shared<std::atomic<int>>(48000 * 3 / 2);
        Running r(600000, [left] { return std::make_unique<UnplugOnce>(left); });
        Client c(r.e->port());
        CHECK(c.hello(r.e->token()));
        CHECK(parse(c.cmd("play", kTwo), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "state") && e["state"].str() == "paused"; }, 10000) >= 0);
        CHECK(c.wait_event([](Json e) { return is_ev(e, "state") && e["state"].str() == "stopped"; }, 300) < 0);
        CHECK(c.wait_event([](Json e) { return is_ev(e, "error"); }, 0) < 0);
        const Json s = parse(c.cmd("status"), d)["result"];
        CHECK_EQ(s["state"].string(), "paused");
        CHECK(s["position_ms"].i64() >= 1000);
        const size_t mark = c.events.size();
        CHECK(parse(c.cmd("resume"), d)["ok"].boolean());
        CHECK(c.wait_event([](Json e) { return is_ev(e, "position") && e["position_ms"].i64() >= 3000; }, 5000, mark) >= 0);
    }
    return check_done();
}
