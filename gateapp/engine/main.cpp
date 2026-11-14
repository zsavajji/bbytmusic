// bbmusicd-gate: the headless engine of the Phase 0 gate app (PLAN.md §9, gates 0.3-0.6).
//   bbmusicd-gate                       run as the headless entry point
//   bbmusicd-gate selftest [s] [dB]     try every command against itself (from a shell, over ssh)
#include <curl/curl.h>
#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <thread>

#include "log.h"
#include "selftest.h"
#include "server.h"

#ifdef __QNX__
#include <bps/bps.h>
#include <bps/navigator.h>
#endif

static Server g_server;

void bbm_exit(int code, const char *why) {
    log_line(std::format("exit {}: {}", code, why));
    g_server.audio().stop();
    g_server.remove_state();
    std::fflush(stdout);
    _exit(code);
}

// A headless app the navigator has not heard from within 6 s of launch is killed, and
// NAVIGATOR_EXIT is how it asks us to go. BPS is per thread: init, request and loop stay here.
static void navigator_thread() {
#ifdef __QNX__
    if (bps_initialize() != BPS_SUCCESS || navigator_request_events(0) != BPS_SUCCESS) {
        log_line("no navigator");
        return;
    }
    log_line("navigator connected");
    for (;;) {
        bps_event_t *ev = nullptr;
        if (bps_get_event(&ev, -1) != BPS_SUCCESS) { log_line("bps_get_event failed"); return; }
        if (ev && bps_event_get_domain(ev) == navigator_get_domain() && bps_event_get_code(ev) == NAVIGATOR_EXIT)
            bbm_exit(0, "NAVIGATOR_EXIT");
    }
#else
    log_line("no navigator");
#endif
}

int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "-h") {  // scripts/push-phone.sh: will the phone run this file?
        std::puts("usage: bbmusicd-gate [selftest [seconds] [gain dB]]");
        return 0;
    }
    std::thread(navigator_thread).detach();  // first thing: the 6 s clock is running
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, [](int) { bbm_exit(0, "SIGTERM"); });  // ponytail: not async-signal-safe; fine for a test app
    const bool selftest = argc > 1 && std::string(argv[1]) == "selftest";
    if (selftest) setenv("BBM_STDERR", "1", 1);
    std::string line = std::format("start pid {} argv", int(getpid()));
    for (int i = 0; i < argc; i++) line += std::format(" {}", argv[i]);
    log_line(line);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::string why;
    g_server.quit_exits = !selftest;
    if (!g_server.start(why)) {
        log_line("server: " + why);
        std::fprintf(stderr, "bbmusicd-gate: %s\n", why.c_str());
        return 1;
    }
    if (selftest) {
        std::thread(&Server::accept_loop, &g_server).detach();
        const double seconds = argc > 2 ? std::atof(argv[2]) : 10, gain = argc > 3 ? std::atof(argv[3]) : -50;
        const bool ok = selftest_run(g_server.port(), g_server.token(), seconds, gain);
        std::puts(ok ? "selftest: all passed" : "selftest: FAILED");
        bbm_exit(ok ? 0 : 1, "selftest done");
    }
    g_server.accept_loop();
}
