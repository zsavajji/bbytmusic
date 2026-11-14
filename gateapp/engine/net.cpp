// From tools/gates/gate03.cpp: libcurl + wolfSSL, TLS 1.3 forced, our own roots.
#include "net.h"

#include <curl/curl.h>

#include <chrono>
#include <format>

namespace {
size_t sink(char *, size_t size, size_t n, void *total) {
    *static_cast<long *>(total) += long(size * n);
    return size * n;
}
}  // namespace

NetResult net_probe(const std::string &cacert, const std::string &proxy) {
    NetResult r;
    CURL *c = curl_easy_init();
    if (!c) { r.error = "curl_easy_init"; return r; }
    char err[CURL_ERROR_SIZE] = "";
    curl_slist *hdr = curl_slist_append(nullptr,
        "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15");
    curl_easy_setopt(c, CURLOPT_URL, "https://music.youtube.com/");
    curl_easy_setopt(c, CURLOPT_CAINFO, cacert.c_str());
    curl_easy_setopt(c, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_3));
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.bytes);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    if (!proxy.empty()) curl_easy_setopt(c, CURLOPT_PROXY, proxy.c_str());
    const auto t0 = std::chrono::steady_clock::now();
    const CURLcode rc = curl_easy_perform(c);
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (rc != CURLE_OK) r.error = std::format("{} ({})", err[0] ? err : curl_easy_strerror(rc), int(rc));
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.http);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    return r;
}
