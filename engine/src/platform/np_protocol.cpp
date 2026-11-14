#include "platform/np_protocol.h"

#include <cstdio>

namespace bbm::np {

std::string json_escape(std::string_view s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += char(c);
        }
    }
    return o;
}

static std::string head(const char *msg, int id) { return std::string("msg::") + msg + "\nid::" + std::to_string(id) + "\n"; }

std::string register_msg(int id, const std::string &name, int pid) {
    return head("register", id) + "dat:json:{\"name\":\"" + json_escape(name) +
           "\",\"prio\":\"low\",\"audio\":\"general\",\"overlay\":\"fancy\",\"pid\":" + std::to_string(pid) + "}\n";
}

std::string acquire_msg(int id) { return head("acquire", id); }

std::string metadata_msg(int id, const std::string &title, const std::string &artist, const std::string &album,
                         int64_t duration_ms, int64_t position_ms, bool next_enabled, bool prev_enabled,
                         const std::string &artwork) {
    std::string m = head("metadata", id) + "dat:json:{\"title\":\"" + json_escape(title) + "\",\"artist\":\"" +
                    json_escape(artist) + "\",\"album\":\"" + json_escape(album) + "\",\"duration\":" +
                    std::to_string(duration_ms) + ",\"position\":" + std::to_string(position_ms) +
                    ",\"nextEnabled\":" + (next_enabled ? "true" : "false") +
                    ",\"prevEnabled\":" + (prev_enabled ? "true" : "false");
    if (!artwork.empty()) {
        const bool url = artwork.rfind("file://", 0) == 0;
        m += ",\"albumArtwork\":\"" + json_escape(url ? artwork : "file://" + artwork) + "\"";
    }
    return m + "}\n";
}

std::string state_msg(int id, const std::string &state) { return head("state", id) + "dat::" + state + "\n"; }

Parsed parse_message(std::string_view msg) {
    Parsed p;
    std::string_view type, dat;
    bool is_res = false;
    while (!msg.empty()) {
        const size_t nl = msg.find('\n');
        std::string_view line = msg.substr(0, nl);
        msg = nl == std::string_view::npos ? std::string_view() : msg.substr(nl + 1);
        if (line.rfind("msg::", 0) == 0) type = line.substr(5);
        else if (line.rfind("res::", 0) == 0) is_res = true;
        else if (line.rfind("err::", 0) == 0) { p.is_error = true; p.error = std::string(line.substr(5)); }
        else if (line.rfind("dat::", 0) == 0) dat = line.substr(5);
    }
    if (is_res) { p.kind = Parsed::Reply; return p; }
    if (type == "revoke") p.kind = Parsed::Revoked;
    else if (type == "track") {
        if (dat == "sendData") p.kind = Parsed::SendData;
        else if (dat == "play" || dat == "pause" || dat == "next") { p.kind = Parsed::Command; p.command = std::string(dat); }
        else if (dat == "prev" || dat == "previous") { p.kind = Parsed::Command; p.command = "prev"; }
    }
    return p;
}

}  // namespace bbm::np
