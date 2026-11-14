// Playing: one track, two tracks gapless, seeks, pause and resume.
//
// Fixtures (tests/fixtures/player/, synthetic, one-second clusters):
//   ffmpeg -f lavfi -i sine=frequency=440:sample_rate=48000:duration=12 -ac 2 -c:a libopus -b:a 48k -cluster_time_limit 1000 tone440_12s.webm
//   ffmpeg -f lavfi -i sine=frequency=880:sample_rate=48000:duration=8  -ac 2 -c:a libopus -b:a 48k -cluster_time_limit 1000 tone880_8s.webm
// The player writes every decoded frame minus the file's pre_skip of 312: 12 s are 576 000 + 648
// frames (the last packet's padding is not trimmed), so a is 576 648 frames; b is 384 648.
#include "check.h"
#include "player/support.h"

using namespace ptest;

static constexpr size_t kA = 576648, kB = 384648;   // frames of a alone, of b alone

static bool wait_ended(Rig &r, int stops = 1) {
    return wait_until([&] { return r.states(PlayState::Stopped) >= stops; });
}

static void single() {
    Rig r;
    r->play({mk("a")}, 0);
    CHECK(wait_ended(r));
    CHECK_EQ(r.sink->frames(), kA);
    CHECK_EQ(r.sink->opens, 1);
    CHECK_EQ(r.sink->closes, 1);
    CHECK_EQ(r.ev.count(PlayerEvent::Track), 1);
    CHECK_EQ(r.states(PlayState::Loading), 1);
    CHECK_EQ(r.states(PlayState::Playing), 1);
    const int pos = r.ev.count(PlayerEvent::Position);   // about once a second of audio
    CHECK(pos >= 11 && pos <= 13);
    CHECK(r->status().duration_ms >= 12000 && r->status().duration_ms <= 12100);
    CHECK(r->status().state == PlayState::Stopped);
}

static void gapless() {
    Rig r;
    r->play({mk("a"), mk("b")}, 0);
    CHECK(wait_ended(r));
    CHECK_EQ(r.sink->frames(), kA + kB);   // the sum of the two alone: nothing added, nothing dropped
    CHECK_EQ(r.sink->opens, 1);
    CHECK_EQ(r.sink->closes, 1);
    // Track events: the load, then the boundary, at the moment b's first sample is written.
    std::vector<size_t> at;
    std::vector<std::string> ids;
    auto evs = r.ev.all();
    for (size_t i = 0; i < evs.size(); i++)
        if (evs[i].kind == PlayerEvent::Track) { ids.push_back(evs[i].status.track_id); at.push_back(r.ev.frames_at[i]); }
    CHECK_EQ(ids.size(), size_t(2));
    if (ids.size() == 2) {
        CHECK_EQ(ids[0], std::string("a"));
        CHECK_EQ(ids[1], std::string("b"));
        CHECK(at[1] >= kA && at[1] <= kA + 5760);
    }
    // The tone changes from 440 Hz to 880 Hz where a ends: find the first 10 ms window that
    // crosses zero like 880 Hz does (about 17.6 times) rather than like 440 Hz (8.8).
    std::vector<int16_t> pcm;
    { std::lock_guard<std::mutex> l(r.sink->m); pcm = r.sink->pcm; }
    CHECK_EQ(crossings(pcm, 2, kA - 30000, kA - 6000), 440);   // half a second of 440 Hz (not the padding)
    const int c2 = crossings(pcm, 2, kA + 2000, kA + 26000);
    CHECK(c2 >= 876 && c2 <= 884);
    size_t boundary = 0;
    for (size_t f = kA - 4800; f < kA + 4800; f += 480)
        if (crossings(pcm, 2, f, f + 480) >= 13) { boundary = f; break; }
    CHECK(boundary > kA - 1500 && boundary < kA + 1500);
    CHECK_EQ(r.opened.size(), size_t(2));
}

static void start_at_position() {
    Rig r;
    r->play({mk("a")}, 0, 5000);   // 7 s to go
    CHECK(wait_ended(r));
    const size_t want = kA - 5000 * 48;
    CHECK(r.sink->frames() + 960 >= want && r.sink->frames() <= want + 960);
}

// Seek while playing, held at the sink so the moment is exact; then count what was written after.
static void seek_playing(int64_t target) {
    Rig r;
    r->play({mk("a")}, 0);
    CHECK(r.gate_at(96000));
    r->seek(target);
    r.release();
    CHECK(wait_ended(r));
    const size_t f0 = r.frames_at_position(target);
    CHECK(f0 != SIZE_MAX);
    const size_t after = r.sink->frames() - f0;
    const size_t want = kA - size_t(target) * 48;   // the rest of the track: within one packet (20 ms)
    CHECK(after + 960 >= want && after <= want + 960);
    CHECK_EQ(r.sink->opens, 1);
    // and it is the same tone: 440 Hz right after the seek
    std::vector<int16_t> pcm;
    { std::lock_guard<std::mutex> l(r.sink->m); pcm = r.sink->pcm; }
    const int c = crossings(pcm, 2, f0 + 100, f0 + 24100);
    CHECK(c >= 436 && c <= 444);
}

static void seek_paused() {
    Rig r;
    r->play({mk("a")}, 0);
    CHECK(r.gate_at(96000));
    r->pause();
    r.release();
    CHECK(wait_state(*r.player, PlayState::Paused));
    const size_t f0 = r.sink->frames();
    CHECK_EQ(r.sink->closes, 1);
    r->seek(5000);
    CHECK(wait_until([&] { return r->status().position_ms == 5000; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_EQ(r.sink->frames(), f0);   // still paused: nothing written
    CHECK(r->status().state == PlayState::Paused);
    r->resume();
    CHECK(wait_ended(r));
    const size_t after = r.sink->frames() - f0;
    CHECK(after + 960 >= kA - 240000 && after <= kA - 240000 + 960);
    CHECK_EQ(r.sink->opens, 2);
}

static void seek_past_end() {
    Rig r;
    r->play({mk("a"), mk("b")}, 0);
    CHECK(r.gate_at(96000));
    r->seek(20000);   // a lasts 12 s
    r.release();
    CHECK(wait_ended(r));
    CHECK_EQ(r.ev.count(PlayerEvent::Track), 2);
    size_t at_b = 0;   // frames in the sink when the load of b was announced
    for (size_t i = 0; i < r.ev.all().size(); i++)
        if (r.ev.all()[i].kind == PlayerEvent::Track) at_b = r.ev.frames_at[i];
    CHECK_EQ(r.sink->frames() - at_b, kB);   // b, whole
    // seeking past the end of the last track ends the queue
    Rig r2;
    r2->play({mk("b")}, 0);
    CHECK(r2.gate_at(48000));
    r2->seek(60000);
    r2.release();
    CHECK(wait_ended(r2));
    CHECK(r2.sink->frames() < kB);
}

static void pause_resume() {
    Rig r;
    r->play({mk("a")}, 0);
    CHECK(r.gate_at(96000));
    r->pause();
    r.release();
    CHECK(wait_state(*r.player, PlayState::Paused));
    const size_t f0 = r.sink->frames();
    CHECK_EQ(r.sink->closes, 1);   // closed once the writes stopped
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK_EQ(r.sink->frames(), f0);
    r->toggle();   // resume
    CHECK(wait_ended(r));
    CHECK_EQ(r.sink->frames(), kA);   // nothing lost, nothing added
    CHECK_EQ(r.sink->opens, 2);
    CHECK_EQ(r.sink->closes, 2);
    // pause again with the toggle, and stop while paused
    Rig r2;
    r2->play({mk("a")}, 0);
    CHECK(r2.gate_at(48000));
    r2->toggle();
    r2.release();
    CHECK(wait_state(*r2.player, PlayState::Paused));
    r2->stop();
    CHECK(wait_state(*r2.player, PlayState::Stopped));
    CHECK_EQ(r2.sink->opens, 1);
}

int main() {
    single();
    gapless();
    start_at_position();
    seek_playing(5000);   // on a cluster boundary
    seek_playing(5500);   // inside a cluster
    seek_playing(300);    // inside the first cluster: the file's pre_skip, not the pre-roll
    seek_paused();
    seek_past_end();
    pause_resume();
    return check_done();
}
