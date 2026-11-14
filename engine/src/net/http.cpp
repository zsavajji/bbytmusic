#include "net/http.h"

#include <curl/curl.h>

#include <cstdlib>
#include <mutex>

namespace bbm {
namespace {
std::mutex g_cfg_mutex;
std::string g_ca_file, g_proxy;

std::string cfg(const std::string &set, const char *env) {
    std::lock_guard<std::mutex> lock(g_cfg_mutex);
    if (!set.empty()) return set;
    const char *e = std::getenv(env);
    return e ? e : "";
}

struct Transfer {
    HttpResponse *resp;
    const HttpRequest *req;
    bool aborted_by_callback = false;
    bool cancelled = false;
};

size_t on_body(char *data, size_t size, size_t n, void *user) {
    auto *t = static_cast<Transfer *>(user);
    const size_t len = size * n;
    if (t->req->on_data) {
        bool keep = false;
        try {
            keep = t->req->on_data(reinterpret_cast<const uint8_t *>(data), len);
        } catch (...) {
        }
        if (!keep) t->aborted_by_callback = true;
        return keep ? len : 0;
    }
    try {
        t->resp->body.append(data, len);
    } catch (...) {
        return 0;
    }
    return len;
}

size_t on_header(char *data, size_t size, size_t n, void *user) {
    auto *t = static_cast<Transfer *>(user);
    const size_t len = size * n;
    try {
        std::string line(data, len);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.compare(0, 5, "HTTP/") == 0) {
            t->resp->headers.clear();  // a redirect's headers give way to the next response's
        } else if (const size_t colon = line.find(':'); colon != std::string::npos && colon > 0) {
            std::string name = line.substr(0, colon);
            for (char &c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            size_t v = colon + 1;
            while (v < line.size() && (line[v] == ' ' || line[v] == '\t')) v++;
            t->resp->headers.emplace_back(std::move(name), line.substr(v));
        }
    } catch (...) {
    }
    return len;
}

int on_progress(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto *t = static_cast<Transfer *>(user);
    if (t->req->cancel && t->req->cancel->load()) {
        t->cancelled = true;
        return 1;
    }
    return 0;
}
}  // namespace

void http_global_init() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}
void http_set_ca_file(const std::string &path) {
    std::lock_guard<std::mutex> lock(g_cfg_mutex);
    g_ca_file = path;
}
void http_set_proxy(const std::string &url) {
    std::lock_guard<std::mutex> lock(g_cfg_mutex);
    g_proxy = url;
}

Http::Http() {
    http_global_init();
    curl_ = curl_easy_init();
}
Http::~Http() { curl_easy_cleanup(static_cast<CURL *>(curl_)); }

HttpResponse Http::send(const HttpRequest &req) {
    HttpResponse resp;
    CURL *c = static_cast<CURL *>(curl_);
    if (!c) {
        resp.error = "curl_easy_init failed";
        return resp;
    }
    curl_slist *list = nullptr;
    try {
        Transfer t{&resp, &req};
        char err[CURL_ERROR_SIZE] = "";
        curl_easy_reset(c);  // drops the options, keeps the connections
        for (const auto &h : req.headers) list = curl_slist_append(list, (h.first + ": " + h.second).c_str());
        curl_easy_setopt(c, CURLOPT_URL, req.url.c_str());
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, list);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_body);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &t);
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &t);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &t);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");  // every encoding curl was built with
        curl_easy_setopt(c, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2));  // 1.2 minimum
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, static_cast<long>(req.timeout_s));
        if (req.on_data) {
            curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
            curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, static_cast<long>(req.timeout_s));
        } else {
            curl_easy_setopt(c, CURLOPT_TIMEOUT, static_cast<long>(req.timeout_s));
        }
        const std::string ca = cfg(g_ca_file, "BBM_CA_FILE");
        if (!ca.empty()) curl_easy_setopt(c, CURLOPT_CAINFO, ca.c_str());
        const std::string proxy = cfg(g_proxy, "BBM_PROXY");
        curl_easy_setopt(c, CURLOPT_PROXY, proxy.c_str());  // "" turns curl's own env lookup off
        if (!proxy.empty()) curl_easy_setopt(c, CURLOPT_NOPROXY, "127.0.0.1,localhost");

        if (req.method == "HEAD") {
            curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
        } else if (req.method == "POST" || !req.body.empty()) {
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(req.body.size()));
            curl_easy_setopt(c, CURLOPT_COPYPOSTFIELDS, req.body.c_str());
            if (req.method != "POST") curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, req.method.c_str());
        } else if (req.method != "GET") {
            curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, req.method.c_str());
        }

        const CURLcode rc = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &resp.status);
        if (rc != CURLE_OK) {
            if (t.cancelled) resp.error = "cancelled";
            else if (t.aborted_by_callback) resp.error = "aborted by on_data";
            else resp.error = std::string(err[0] ? err : curl_easy_strerror(rc)) + " (" + std::to_string(int(rc)) + ")";
        }
    } catch (const std::exception &e) {
        resp.error = std::string("http: ") + e.what();
    } catch (...) {
        resp.error = "http: unknown exception";
    }
    curl_slist_free_all(list);
    return resp;
}
}  // namespace bbm
