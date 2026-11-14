// UmpReader fed in chunks (byte 0 picks the size, 0 = one feed); also the bare varint reader.
#include <cstdint>
#include <span>

#include "fuzz.h"
#include "stream/ump.h"

using namespace bbm;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) return 0;
    const size_t chunk = data[0] ? data[0] : size;
    std::span<const uint8_t> all(data + 1, size - 1);
    UmpReader r;
    size_t fed = 0, got = 0;
    uint64_t sum = 0;
    for (size_t off = 0; off < all.size(); off += chunk) {
        auto c = all.subspan(off, std::min(chunk, all.size() - off));
        r.feed(c);
        fed += c.size();
        uint32_t type;
        std::span<const uint8_t> payload;
        while (r.next(type, payload)) {
            got += payload.size();
            for (uint8_t b : payload) sum += b;
        }
        FUZZ_ASSERT(got + r.pending() <= fed);                     // never more out than went in
    }
    size_t pos = 0;
    uint32_t v;
    while (pos < all.size() && ump_read_varint(all, pos, v)) FUZZ_ASSERT(pos <= all.size());
    return sum == ~0ull;
}
