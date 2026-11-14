// The InnerTube clients resolve() asks /player through, as data (phase0.md, gate 0.1).
#pragma once
#include <span>
#include <string_view>

namespace bbm {

struct PlayerClient {
    const char *name;          // clientName, "VISIONOS"
    const char *version;       // clientVersion
    int id;                    // X-YouTube-Client-Name, and StreamerContext's client_name
    const char *user_agent;
    // extra fields of context.client; "" or 0 are left out
    const char *device_make, *device_model, *os_name, *os_version;
    int android_sdk;
};

std::span<const PlayerClient> player_clients();             // in the order resolve() tries them
const PlayerClient *find_player_client(std::string_view name);   // nullptr if not in the table

}  // namespace bbm
