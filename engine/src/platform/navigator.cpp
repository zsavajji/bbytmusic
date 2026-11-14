#include "platform/navigator.h"

#include <thread>

#include "util/log.h"

#ifdef __QNX__
#include <bps/bps.h>
#include <bps/navigator.h>
#endif

namespace bbm {

void Navigator::start([[maybe_unused]] std::function<void()> on_exit) {
#ifdef __QNX__
    // BPS is per thread: init, request and loop all stay on this one, which is never joined.
    std::thread([on_exit = std::move(on_exit)] {
        if (bps_initialize() != BPS_SUCCESS || navigator_request_events(0) != BPS_SUCCESS) {
            log_warn("no navigator");
            return;
        }
        log_info("navigator connected");
        for (;;) {
            bps_event_t *ev = nullptr;
            if (bps_get_event(&ev, -1) != BPS_SUCCESS) { log_warn("bps_get_event failed"); return; }
            if (ev && bps_event_get_domain(ev) == navigator_get_domain() && bps_event_get_code(ev) == NAVIGATOR_EXIT) {
                log_info("NAVIGATOR_EXIT");
                if (on_exit) on_exit();
            }
        }
    }).detach();
#else
    log_info("no navigator");
#endif
}

}  // namespace bbm
