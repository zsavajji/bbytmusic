// parse_player_response over JSON text, with each client of the table (byte 0 picks it).
#include <cstdint>
#include <string>
#include <string_view>

#include "fuzz.h"
#include "stream/clients.h"
#include "stream/resolve.h"

using namespace bbm;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) return 0;
    auto clients = player_clients();
    const PlayerClient &c = clients[data[0] % clients.size()];
    StreamInfo info;
    std::string err;
    const bool ok = parse_player_response({reinterpret_cast<const char *>(data + 1), size - 1}, c, info, err);
    if (!ok) FUZZ_ASSERT(!err.empty());
    else FUZZ_ASSERT(!info.url.empty() || !info.sabr_url.empty());   // "neither" is a failure by contract
    return 0;
}
