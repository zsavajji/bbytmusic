// WebmDemuxer: byte 0 picks the chunk size (0 = one feed), the rest is the stream.
#include <cstdint>
#include <span>
#include <string>

#include "fuzz.h"
#include "media/webm_demux.h"

using namespace bbm;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) return 0;
    const size_t chunk = data[0] ? data[0] : size;
    std::span<const uint8_t> all(data + 1, size - 1);
    WebmDemuxer d;
    uint64_t sum = 0;
    for (size_t off = 0; off < all.size(); off += chunk) {
        std::string err;
        const bool ok = d.feed(all.subspan(off, std::min(chunk, all.size() - off)), err);
        FUZZ_ASSERT(ok || !err.empty());
        Packet p;
        while (d.next(p)) {
            FUZZ_ASSERT(p.data != nullptr || p.size == 0);
            FUZZ_ASSERT(p.size <= (16u << 20));            // a leaf is never buffered bigger than that
            for (size_t i = 0; i < p.size; i++) sum += p.data[i];   // ASan checks it is owned memory
        }
        if (!ok) break;
    }
    d.reset();
    return sum == 0xFFFFFFFFFFFFFFFFull;   // keeps the reads alive
}
