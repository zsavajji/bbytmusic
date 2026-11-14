// PbReader and PbMsg: walk the fields, treat length-delimited values as nested messages.
#include <cstdint>
#include <span>

#include "fuzz.h"
#include "proto/pb.h"

using namespace bbm;

static void walk(std::span<const uint8_t> buf, int depth) {
    PbMsg m(buf);
    PbReader r(buf);
    PbField f;
    size_t n = 0;
    while (r.next(f)) {
        n++;
        if (f.wire == PbWire::Bytes) {
            const uint8_t *lo = buf.data(), *hi = lo + buf.size();
            FUZZ_ASSERT(f.bytes.empty() || (f.bytes.data() >= lo && f.bytes.data() + f.bytes.size() <= hi));
            for (uint8_t b : f.bytes) (void)b;
            if (depth < 4) walk(f.bytes, depth + 1);
        } else {
            (void)f.f32();
        }
        if (m.ok()) {
            (void)m.u(f.number);
            (void)m.str(f.number);
            FUZZ_ASSERT(m.has(f.number));
        }
    }
    FUZZ_ASSERT(m.ok() == !r.failed());   // PbMsg and PbReader agree on what is malformed
    (void)n;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    walk({data, size}, 0);
    size_t pos = 0;
    uint64_t v;
    if (PbReader::read_varint({data, size}, pos, v)) FUZZ_ASSERT(pos >= 1 && pos <= 10 && pos <= size);
    return 0;
}
