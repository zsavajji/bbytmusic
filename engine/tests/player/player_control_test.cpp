// Controls and failure handling: next, previous, jump, queue edits, QueueLow, failing tracks,
// stop and the destructor with a source that sleeps, and the bounded read-ahead.
// Tracks "a" and "c".."f" play the 440 Hz file (576 648 frames), "b" the 880 Hz one (384 648).
#include "check.h"
#include "player/support.h"

using namespace ptest;

static constexpr size_t kA = 576648, kB = 384648;

static bool wait_ended(Rig &r, int stops = 1) {
    return wait_until([&] { return r.states(PlayState::Stopped) >= stops; });
}
static std::vector<std::string> track_ids(Rig &r) {
    std::vector<std::string> ids;
    for (auto &e : r.ev.all())
        if (e.kind == PlayerEvent::Track) ids.push_back(e.status.track_id);
    return ids;
}
using V = std::vector<std::string>;

static void next_prev_jump() {
    Rig r;
    r->play({mk("a"), mk("b"), mk("c")}, 0);
    CHECK(r.gate_at(48000));
    r->next();
    r.release();   // the player is held inside a write until now
    CHECK(wait_until([&] { return r->status().track_id == "b" && r->status().index == 1; }));
    CHECK(r.gate_at(r.sink->frames() + 48000));   // b is playing: hold it a second further on
    r->jump(2);
    r.release();
    CHECK(wait_until([&] { return r->status().track_id == "c"; }));
    CHECK(r.gate_at(r.sink->frames() + 48000));
    r->previous();   // less than 3 s in: the track before
    r.release();
    CHECK(wait_until([&] { return r->status().index == 1 && r->status().track_id == "b"; }));
    CHECK(wait_until([&] { return r.sink->frames() > 0 && r->status().state == PlayState::Playing; }));
    r->stop();
    CHECK(wait_ended(r));
    CHECK(track_ids(r) == (V{"a", "b", "c", "b"}));
    CHECK_EQ(r.sink->opens, 1);   // the sink stayed open across all of it
}

static void previous_restarts() {
    Rig r;
    r->play({mk("a"), mk("b")}, 1, 5000);   // b, 5 s in
    CHECK(wait_until([&] { return r->status().state == PlayState::Playing; }));
    CHECK(r.gate_at(r.sink->frames() + 48000));
    CHECK(r->status().position_ms >= 5000);
    r->previous();   // more than 3 s in: this track again, from the start
    r.release();
    CHECK(wait_until([&] { return r.ev.count(PlayerEvent::Track) == 2; }));
    CHECK(wait_ended(r));
    CHECK(track_ids(r) == (V{"b", "b"}));
    // the first track, less than 3 s in: nothing before it, so it restarts
    Rig r2;
    r2->play({mk("a"), mk("b")}, 0);
    CHECK(r2.gate_at(48000));
    r2->previous();
    r2.release();
    CHECK(wait_until([&] { return r2.ev.count(PlayerEvent::Track) == 2; }));
    CHECK(wait_ended(r2));
    CHECK_EQ(r2->status().index, 1);   // ran through both
    // next at the end of the queue stops
    Rig r3;
    r3->play({mk("b")}, 0);
    CHECK(r3.gate_at(48000));
    r3->next();
    r3.release();
    CHECK(wait_ended(r3));
    CHECK(r3.sink->frames() < kB);
}

static void failing_tracks() {
    Rig r;
    r.tweak = [](const std::string &id, FileSource::Options &o) { o.fail_open = id.rfind("bad", 0) == 0; };
    r->play({mk("bad1"), mk("b")}, 0);
    CHECK(wait_ended(r));
    CHECK_EQ(r.ev.count(PlayerEvent::Error), 1);
    CHECK_EQ(r.sink->frames(), kB);   // skipped, then b played
    CHECK(track_ids(r).size() >= 1);
    for (auto &e : r.ev.all())
        if (e.kind == PlayerEvent::Error) CHECK(e.status.error.find("bad1") != std::string::npos);

    // a failure in the middle of a track: what was read is played, then the next track
    Rig r2;
    r2.tweak = [](const std::string &id, FileSource::Options &o) { if (id == "mid") o.fail_at = 3; };
    r2->play({mk("mid"), mk("b")}, 0);
    CHECK(wait_ended(r2));
    CHECK_EQ(r2.ev.count(PlayerEvent::Error), 1);
    const size_t first = r2.sink->frames() - kB;   // three clusters of the first track
    CHECK(first > 140000 && first < 146000);
    CHECK_EQ(r2.sink->opens, 1);

    // a bad track after the first one is reported when its turn comes, and skipped
    Rig r3;
    r3.tweak = [](const std::string &id, FileSource::Options &o) { o.fail_open = id.rfind("bad", 0) == 0; };
    r3->play({mk("b"), mk("bad1"), mk("b")}, 0);
    CHECK(wait_ended(r3));
    CHECK_EQ(r3.ev.count(PlayerEvent::Error), 1);
    CHECK_EQ(r3.sink->frames(), 2 * kB);
    CHECK_EQ(r3.sink->opens, 1);

    // three in a row stop the player
    Rig r4;
    r4.tweak = [](const std::string &id, FileSource::Options &o) { o.fail_open = id.rfind("bad", 0) == 0; };
    r4->play({mk("bad1"), mk("bad2"), mk("bad3"), mk("a")}, 0);
    CHECK(wait_ended(r4));
    CHECK_EQ(r4.ev.count(PlayerEvent::Error), 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_EQ(r4.sink->frames(), size_t(0));
    CHECK(r4->status().state == PlayState::Stopped);
    CHECK_EQ(r4->status().index, 2);

    // two failures and a good track: the count starts again
    Rig r5;
    r5.tweak = [](const std::string &id, FileSource::Options &o) { o.fail_open = id.rfind("bad", 0) == 0; };
    r5->play({mk("bad1"), mk("bad2"), mk("b"), mk("bad3"), mk("bad4"), mk("b")}, 0);
    CHECK(wait_ended(r5));
    CHECK_EQ(r5.ev.count(PlayerEvent::Error), 4);
    CHECK_EQ(r5.sink->frames(), 2 * kB);
}

static void queue_edits() {
    // enqueue with play_next on the only track: b follows it
    Rig r;
    r->play({mk("a")}, 0);
    CHECK(r.gate_at(96000));
    r->enqueue({mk("b")}, true);
    r.release();
    CHECK(wait_ended(r));
    CHECK_EQ(r.sink->frames(), kA + kB);
    CHECK(r.ev.count(PlayerEvent::Queue) >= 2);

    // remove the prepared next: c plays after a, b never does
    Rig r2;
    r2->play({mk("a"), mk("b"), mk("c")}, 0);
    CHECK(r2.gate_at(96000));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));   // let the feed prepare b
    r2->remove(1);
    r2.release();
    CHECK(wait_ended(r2));
    CHECK(track_ids(r2) == (V{"a", "c"}));
    CHECK_EQ(r2.sink->frames(), 2 * kA);
    CHECK_EQ(r2->queue().size(), size_t(2));

    // move a track in front of the prepared one
    Rig r3;
    r3->play({mk("a"), mk("b"), mk("c")}, 0);
    CHECK(r3.gate_at(96000));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    r3->move(2, 1);
    r3.release();
    CHECK(wait_ended(r3));
    CHECK(track_ids(r3) == (V{"a", "c", "b"}));
    CHECK_EQ(r3.sink->frames(), 2 * kA + kB);

    // remove the current track: the one after it plays at once
    Rig r4;
    r4->play({mk("a"), mk("b")}, 0);
    CHECK(r4.gate_at(96000));
    r4->remove(0);
    r4.release();
    CHECK(wait_ended(r4));
    CHECK(track_ids(r4) == (V{"a", "b"}));
    CHECK_EQ(r4->queue().size(), size_t(1));
    CHECK_EQ(r4->status().index, 0);
}

static void queue_low() {
    Rig r;
    r->play({mk("a"), mk("b"), mk("c"), mk("d")}, 0);   // 3 after the first: not low yet
    CHECK(wait_ended(r));
    int first_low = -1, n = 0;
    auto evs = r.ev.all();
    for (size_t i = 0; i < evs.size(); i++)
        if (evs[i].kind == PlayerEvent::QueueLow) { if (first_low < 0) first_low = int(i); n++; }
    CHECK(n >= 1);
    // the first one comes with b becoming current (2 left), after a was fully written
    CHECK(first_low >= 0 && r.ev.frames_at[size_t(first_low)] >= kA);
    CHECK_EQ(evs[size_t(first_low)].status.index, 1);
    Rig r2;   // a single track is low from the start
    r2->play({mk("a")}, 0);
    CHECK(wait_ended(r2));
    CHECK(r2.ev.count(PlayerEvent::QueueLow) >= 1);
}

static double ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

static void stop_and_destroy_are_quick() {
    Rig r;
    r.tweak = [](const std::string &, FileSource::Options &o) { o.delay_ms = 2000; };
    r->play({mk("a")}, 0);
    CHECK(wait_until([&] { std::lock_guard<std::mutex> l(r.m); return !r.opened.empty(); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));   // the first read is sleeping now
    auto t0 = std::chrono::steady_clock::now();
    r->stop();
    CHECK(ms_since(t0) < 50);   // returns at once
    CHECK(wait_state(*r.player, PlayState::Stopped));
    CHECK(ms_since(t0) < 200);
    // a new play after the stop works (the cancelled source is not reused)
    r.tweak = nullptr;
    r->play({mk("b")}, 0);
    CHECK(wait_until([&] { return r.states(PlayState::Stopped) >= 2 && r.sink->frames() == kB; }));

    Rig r2;
    r2.tweak = [](const std::string &, FileSource::Options &o) { o.delay_ms = 2000; };
    r2->play({mk("a")}, 0);
    CHECK(wait_until([&] { std::lock_guard<std::mutex> l(r2.m); return !r2.opened.empty(); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    t0 = std::chrono::steady_clock::now();
    r2.player.reset();   // the destructor
    CHECK(ms_since(t0) < 200);

    // a seek while a read sleeps: cancelled, the track restarts there
    Rig r3;
    r3.tweak = [](const std::string &, FileSource::Options &o) { o.delay_ms = 300; };
    r3->play({mk("b")}, 0);
    CHECK(wait_until([&] { return r3->status().state == PlayState::Playing; }));
    r3->seek(7000);
    CHECK(wait_ended(r3));
    CHECK(r3.sink->frames() < kB);
}

// A slow sink (real time) with a fast source: the feed stops about 30 s ahead.
static void read_ahead_is_bounded() {
    Rig r;
    r.sink->realtime = true;
    r.tweak = [](const std::string &, FileSource::Options &o) { o.loops = 10; };   // 130 s
    auto t0 = std::chrono::steady_clock::now();
    r->play({mk("a")}, 0);
    CHECK(wait_until([&] { return r->reads_for_test() >= 30; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    const double played_s = ms_since(t0) / 1000;
    // A loose bound on purpose: clusters are 981 ms, the clock is the wall's, and the count is
    // read a moment after the time. The exact bound is the buffered time, two lines down.
    CHECK(r->reads_for_test() <= 31 + int(played_s) + 4);
    CHECK(r->reads_for_test() >= 30);
    CHECK(r->buffered_ms_for_test() <= 31000);
    CHECK(r->buffered_ms_for_test() >= 28000);
    CHECK(r->status().state == PlayState::Playing);
    // the sound is the sound: roughly real time, not racing ahead
    CHECK(r.sink->frames() < size_t(played_s * 48000 + 24000));
}

int main() {
    next_prev_jump();
    previous_restarts();
    failing_tracks();
    queue_edits();
    queue_low();
    stop_and_destroy_are_quick();
    read_ahead_is_bounded();
    return check_done();
}
