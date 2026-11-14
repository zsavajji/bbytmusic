// np::parse_message over arbitrary text; json_escape round trip through the JSON parser.
#include <cstdint>
#include <string>
#include <string_view>

#include "fuzz.h"
#include "platform/np_protocol.h"
#include "util/json.h"

using namespace bbm;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string_view msg(reinterpret_cast<const char *>(data), size);
    np::Parsed p = np::parse_message(msg);
    FUZZ_ASSERT(p.kind == np::Parsed::Command || p.command.empty());
    if (p.kind == np::Parsed::Command) FUZZ_ASSERT(!p.command.empty());
    (void)np::json_escape(msg);
    return 0;
}
