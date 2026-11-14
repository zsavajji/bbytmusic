// bbmusicd, the engine: bbmusicd [--data DIR] [--wav FILE | --null] | --version
// It listens on loopback (docs/protocol.md) until quit, a signal, the navigator's exit, or ten
// minutes with nothing playing and no client.
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "audio/null_sink.h"
#include "audio/qsa_sink.h"
#include "audio/wav_sink.h"
#include "engine/engine.h"
#include "engine/paced_sink.h"
#include "net/http.h"
#include "platform/navigator.h"
#include "util/log.h"

#ifndef BBM_VERSION
#define BBM_VERSION "0"
#endif

namespace {

std::atomic<bbm::Engine *> g_engine{nullptr};
std::atomic<bool> g_exit{false};

void request_exit() {
    g_exit = true;
    if (bbm::Engine *e = g_engine) e->stop();
}

bool make_dirs(const std::string &path) {
    for (size_t i = 1; i <= path.size(); i++)
        if (i == path.size() || path[i] == '/')
            if (::mkdir(path.substr(0, i).c_str(), 0700) != 0 && errno != EEXIST) return false;
    return true;
}

int usage() {
    std::fprintf(stderr, "usage: bbmusicd [--data DIR] [--wav FILE | --null] | --version\n");
    return 2;
}

}  // namespace

int main(int argc, char **argv) {
    std::string data, wav;
    bool null_sink = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--version") {
            std::printf("bbmusicd %s\n", BBM_VERSION);
            return 0;
        }
        if (a == "--data" && i + 1 < argc) data = argv[++i];
        else if (a == "--wav" && i + 1 < argc) wav = argv[++i];
        else if (a == "--null") null_sink = true;
        // The navigator starts a headless entry point with arguments of its own (the package's
        // name, "invoke://localhost"): 0.1.0.3 took them for a mistake and exited in 80 ms.
        else if (a.rfind("--", 0) == 0) return usage();
    }

    // SIGINT and SIGTERM go to one thread that stops the engine cleanly (engine.json removed).
    // Blocked before any other thread exists, so that every thread inherits the mask.
    sigset_t sigs;
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGINT);
    sigaddset(&sigs, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigs, nullptr);
    // Then the navigator: it slays a headless process that has not reached it within 6 s.
    bbm::Navigator().start(request_exit);
    std::thread([sigs] {
        int sig = 0;
        if (sigwait(&sigs, &sig) == 0) {
            bbm::log_info(std::string("signal ") + std::to_string(sig) + ": stopping");
            request_exit();
        }
    }).detach();

    if (data.empty()) {
        const char *home = std::getenv("HOME");
        data = home && *home ? home : "data";
    }
    if (!make_dirs(data)) {
        std::fprintf(stderr, "bbmusicd: cannot create %s: %s\n", data.c_str(), std::strerror(errno));
        return 1;
    }
    const std::string log_path = data + "/bbmusicd.log";
    if (!bbm::set_log_file(log_path)) std::fprintf(stderr, "bbmusicd: cannot write %s; logging here\n", log_path.c_str());
    bbm::log_info(std::string("bbmusicd ") + BBM_VERSION + ", data in " + data);
#ifdef __QNX__
    // The phone's own roots are from 2013: the bundle packaged with the app.
    if (::access("app/native/assets/cacert.pem", R_OK) == 0) bbm::http_set_ca_file("app/native/assets/cacert.pem");
#endif

    bbm::EngineOptions o;
    o.data_dir = data;
    o.version = BBM_VERSION;
    auto paced_null = [] { return std::make_unique<bbm::PacedSink>(std::make_unique<bbm::NullSink>()); };
    if (!wav.empty()) {
        o.sinks = [wav] { return std::make_unique<bbm::PacedSink>(std::make_unique<bbm::WavSink>(wav)); };
    } else if (null_sink) {
        o.sinks = paced_null;
    } else {
#ifdef __QNX__
        o.sinks = [] { return std::make_unique<bbm::QsaSink>(); };
#else
        o.sinks = paced_null;
#endif
    }

    bbm::Engine engine(std::move(o));
    std::string err;
    if (!engine.start(err)) {
        bbm::log_error(err);
        std::fprintf(stderr, "bbmusicd: %s\n", err.c_str());
        return 1;
    }
    std::fprintf(stderr, "bbmusicd: listening on 127.0.0.1:%d; %s/engine.json, log in %s\n", engine.port(),
                 data.c_str(), log_path.c_str());
    g_engine = &engine;
    if (g_exit) engine.stop();
    engine.run();
    g_engine = nullptr;
    bbm::log_info("bye");
    return 0;
}
