#pragma once
#include <string>

struct NetResult {
    long http = 0;
    long bytes = 0;
    double seconds = 0;
    std::string error;
};

// GET https://music.youtube.com/ over TLS 1.3 (forced) with this CA bundle; proxy "" for none.
NetResult net_probe(const std::string &cacert, const std::string &proxy);
