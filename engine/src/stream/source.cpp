#include "stream/source.h"

#include "stream/dash_source.h"
#include "stream/sabr_source.h"

namespace bbm {

std::unique_ptr<Source> make_source(const StreamInfo &info, bool prefer_sabr) {
    const bool dash = !info.url.empty(), sabr = !info.sabr_url.empty() && !info.ustreamer_config.empty();
    if (sabr && (prefer_sabr || !dash)) return std::make_unique<SabrSource>(info);
    if (dash) return std::make_unique<DashSource>(info);
    return nullptr;
}

}  // namespace bbm
