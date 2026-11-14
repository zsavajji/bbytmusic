// The now-playing service's PPS messages (phase0.md, gate 0.5): pure text in, text out, so the
// Mac tests the real code. No platform code here.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace bbm::np {

std::string json_escape(std::string_view s);   // quotes, backslash, control chars; UTF-8 kept as is

std::string register_msg(int id, const std::string &name, int pid);
std::string acquire_msg(int id);
// artwork: a file path (or file:// url); "" leaves albumArtwork out.
std::string metadata_msg(int id, const std::string &title, const std::string &artist, const std::string &album,
                         int64_t duration_ms, int64_t position_ms, bool next_enabled, bool prev_enabled,
                         const std::string &artwork);
std::string state_msg(int id, const std::string &state);   // "playing" | "paused" | "stopped"

struct Parsed {
    enum Kind { Ignore, Command, SendData, Reply, Revoked } kind = Ignore;
    std::string command;   // Command: play, pause, next, prev
    bool is_error = false;  // Reply: it carried err::
    std::string error;      // Reply: the text after err::
};
// One message as the service writes it (an optional "@control" first line, then key::value lines).
Parsed parse_message(std::string_view msg);

}  // namespace bbm::np
