// From a video id to a StreamInfo, through /youtubei/v1/player, anonymous, no PO token.
#pragma once
#include <string>
#include <string_view>

#include "stream/clients.h"
#include "stream/source.h"

namespace bbm {

// The visitor data sent with /player. Unless set here first (the InnerTube module shares its
// own), resolve() fetches it once, from a WEB_REMIX search's responseContext.visitorData.
void set_visitor_data(std::string visitor);

// Tries the clients of player_clients() in order: the first that gives a direct url wins;
// otherwise the first that gives SABR. err lists what each client said. When the network itself
// fails (no DNS, no connection: no client got an answer), it waits and tries again, up to ~15 s.
bool resolve(const std::string &video_id, StreamInfo &out, std::string &err);

// One client's answer: true if it gives a direct url or SABR for the format. *net_error is set
// when the request got no answer at all (the transfer failed).
bool resolve_client(const std::string &video_id, const PlayerClient &client, StreamInfo &out, std::string &err,
                    bool *net_error = nullptr);

// One /player response: the audio format to play (Opus 251, else 250, else 249; AAC 140 only
// if there is no Opus) and everything StreamInfo holds. False if it is not playable, or has
// neither a direct url nor SABR for that format.
bool parse_player_response(std::string_view json, const PlayerClient &client, StreamInfo &out, std::string &err);

}  // namespace bbm
