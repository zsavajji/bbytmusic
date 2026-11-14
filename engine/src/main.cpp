#include <cstdio>
#include <cstring>

#include "net/http.h"

#ifndef BBM_VERSION
#define BBM_VERSION "0"
#endif

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--version")) {
            std::printf("bbmusicd %s\n", BBM_VERSION);
            return 0;
        }
        if (!std::strcmp(argv[i], "--get") && i + 1 < argc) {  // a smoke test of net + TLS on the device
            bbm::Http http;
            bbm::HttpRequest req;
            req.url = argv[i + 1];
            const bbm::HttpResponse r = http.send(req);
            std::printf("status %ld, %zu bytes%s%s\n", r.status, r.body.size(), r.error.empty() ? "" : ", error: ", r.error.c_str());
            return r.ok() ? 0 : 1;
        }
    }
    std::fprintf(stderr, "usage: bbmusicd --version | --get <url>\n");
    return 0;
}
