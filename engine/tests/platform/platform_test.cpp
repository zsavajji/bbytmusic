#include <string>

// The test links only `platform`, so QsaSink (module `audio`) is compiled in directly.
#include "../../src/audio/qsa_sink.cpp"
#include "check.h"
#include "platform/navigator.h"
#include "platform/now_playing.h"
#include "platform/np_protocol.h"

using namespace bbm;

int main() {
    // The stubs fail cleanly.
    std::string err;
    QsaSink sink;
    CHECK(!sink.open(48000, 2, err));
    CHECK_EQ(err, std::string("not on this platform"));
    int16_t s[2] = {0, 0};
    CHECK(!sink.write(s, 1, err));
    CHECK_EQ(sink.underruns(), 0);
    sink.close();
    NowPlaying np;
    err.clear();
    CHECK(!np.start("x", [](const std::string &) {}, err));
    CHECK_EQ(err, std::string("not on this platform"));
    CHECK(!np.acquire(err));
    np.set_metadata("t", "a", "b", 1, 0, true, true);
    np.set_state(NowPlaying::State::Playing);
    np.stop();
    bool exited = false;
    Navigator().start([&] { exited = true; });
    CHECK(!exited);

    // The messages, as phase0.md shows them.
    CHECK_EQ(np::register_msg(1, "bbmusic-probe", 42),
             "msg::register\nid::1\ndat:json:{\"name\":\"bbmusic-probe\",\"prio\":\"low\",\"audio\":\"general\",\"overlay\":\"fancy\",\"pid\":42}\n");
    CHECK_EQ(np::acquire_msg(2), "msg::acquire\nid::2\n");
    CHECK_EQ(np::metadata_msg(3, "Song", "Artist", "Album", 600000, 0, true, true, ""),
             "msg::metadata\nid::3\ndat:json:{\"title\":\"Song\",\"artist\":\"Artist\",\"album\":\"Album\",\"duration\":600000,\"position\":0,\"nextEnabled\":true,\"prevEnabled\":true}\n");
    CHECK_EQ(np::metadata_msg(3, "Say \"hi\" \\ caf\xc3\xa9\n", "A", "B", 1, 2, false, true, "/tmp/a.jpg"),
             "msg::metadata\nid::3\ndat:json:{\"title\":\"Say \\\"hi\\\" \\\\ caf\xc3\xa9\\n\",\"artist\":\"A\",\"album\":\"B\",\"duration\":1,\"position\":2,\"nextEnabled\":false,\"prevEnabled\":true,\"albumArtwork\":\"file:///tmp/a.jpg\"}\n");
    CHECK_EQ(np::state_msg(4, "playing"), "msg::state\nid::4\ndat::playing\n");

    // What the service sends.
    auto p = np::parse_message("@control\nmsg::track\ndat::pause\n");
    CHECK(p.kind == np::Parsed::Command);
    CHECK_EQ(p.command, std::string("pause"));
    CHECK_EQ(np::parse_message("@control\nmsg::track\ndat::play\n").command, std::string("play"));
    CHECK_EQ(np::parse_message("@control\nmsg::track\ndat::next\n").command, std::string("next"));
    CHECK_EQ(np::parse_message("@control\nmsg::track\ndat::prev\n").command, std::string("prev"));
    CHECK(np::parse_message("@control\nmsg::track\ndat::sendData\n").kind == np::Parsed::SendData);
    CHECK(np::parse_message("@control\nmsg::track\ndat::holdData\n").kind == np::Parsed::Ignore);
    p = np::parse_message("@control\nres::state\nid::4\n");
    CHECK(p.kind == np::Parsed::Reply && !p.is_error);
    p = np::parse_message("@control\nres::state\nid::5\nerr::badparam\n");
    CHECK(p.kind == np::Parsed::Reply && p.is_error);
    CHECK_EQ(p.error, std::string("badparam"));
    p = np::parse_message("@control\nmsg::revoke\ndat:json:{\"pid\":\"7\",\"name\":\"other\"}\n");
    CHECK(p.kind == np::Parsed::Revoked);
    CHECK(np::parse_message("").kind == np::Parsed::Ignore);
    return check_done();
}
