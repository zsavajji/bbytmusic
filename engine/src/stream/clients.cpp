#include "stream/clients.h"

namespace bbm {
namespace {

// tools/gates/player_matrix.py, 2026-11-14: VISIONOS gives direct urls and SABR; IOS and
// ANDROID give SABR only. ANDROID_VR's urls stop at 1 MB, and the web clients need a PO token.
const char kSafariUa[] = "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 "
                         "(KHTML, like Gecko) Version/26.0 Safari/605.1.15";

const PlayerClient kClients[] = {
    {"VISIONOS", "1.02", 101, kSafariUa, "Apple", "RealityDevice17,1", "visionOS", "26.5.23O471", 0},
    {"IOS", "21.26.4", 5, "com.google.ios.youtube/21.26.4 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)",
     "Apple", "iPhone16,2", "iPhone", "18.3.2.22D82", 0},
    {"ANDROID", "21.26.364", 3, "com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip", "", "",
     "Android", "11", 30},
};

}  // namespace

std::span<const PlayerClient> player_clients() { return kClients; }

const PlayerClient *find_player_client(std::string_view name) {
    for (const PlayerClient &c : kClients)
        if (name == c.name) return &c;
    return nullptr;
}

}  // namespace bbm
