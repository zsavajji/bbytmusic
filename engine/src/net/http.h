// libcurl behind a small interface. One Http per thread: it owns one curl easy handle and
// reuses it, so connections are kept alive between requests to the same host.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace bbm {

struct HttpRequest {
    std::string method = "GET", url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    // Seconds. For a buffered response the whole transfer must finish within it; when on_data
    // is set it is the connect timeout and the longest the transfer may stall (under 1 byte/s).
    int timeout_s = 30;
    std::function<bool(const uint8_t *, size_t)> on_data;  // if set, the body is streamed to it
                                                           // (response.body stays empty); false aborts
    const std::atomic<bool> *cancel = nullptr;             // set to true from another thread to abort
};

struct HttpResponse {
    long status = 0;                 // 0 if no response arrived
    std::string body;
    std::string error;               // empty unless the transfer failed (also "cancelled", "aborted by on_data")
    std::vector<std::pair<std::string, std::string>> headers;   // of the final response; names lowercased
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
    // Value of a header (name in lowercase), or "".
    std::string header(const std::string &lower_name) const {
        for (const auto &h : headers)
            if (h.first == lower_name) return h.second;
        return {};
    }
};

class Http {
public:
    Http();
    ~Http();
    Http(const Http &) = delete;
    Http &operator=(const Http &) = delete;
    HttpResponse send(const HttpRequest &);   // never throws; blocks until done, cancelled or failed
private:
    void *curl_;
};

void http_global_init();                          // idempotent; Http() calls it too
void http_set_ca_file(const std::string &path);   // else $BBM_CA_FILE, else the system's roots
void http_set_proxy(const std::string &url);      // else $BBM_PROXY, else none (curl's own env is ignored)

}  // namespace bbm
