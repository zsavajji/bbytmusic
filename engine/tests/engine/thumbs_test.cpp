// The thumbnail cache: sized_url, a cached file found without a fetch, and a real fetch from a
// loopback server of our own.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <thread>

#include "check.h"
#include "engine/thumbs.h"
#include "util/sha1.h"

using namespace bbm;

namespace {

const std::string kBody = "\xFF\xD8\xFF\xE0 not really a jpeg";

// Answers every connection with kBody, once each, until closed; counts them.
struct Server {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int port = 0;
    std::atomic<int> hits{0};
    std::thread t;
    Server() {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof a);
        ::listen(fd, 4);
        socklen_t n = sizeof a;
        ::getsockname(fd, reinterpret_cast<sockaddr *>(&a), &n);
        port = ntohs(a.sin_port);
        t = std::thread([this] {
            for (;;) {
                const int c = ::accept(fd, nullptr, nullptr);
                if (c < 0) return;
                char buf[4096];
                ::recv(c, buf, sizeof buf, 0);   // the request fits in one read
                const std::string r = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " +
                                      std::to_string(kBody.size()) + "\r\n\r\n" + kBody;
                ::send(c, r.data(), r.size(), 0);
                ::close(c);
                hits++;
            }
        });
    }
    ~Server() {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
        t.join();
    }
};

}  // namespace

int main() {
    const std::string g = "https://lh3.googleusercontent.com/abc";
    CHECK_EQ(Thumbs::sized_url(g + "=w120-h120-l90-rj", 544), g + "=w544-h544-l90-rj");
    CHECK_EQ(Thumbs::sized_url("https://yt3.googleusercontent.com/x=s88-c-k-c0x00ffffff-no-rj", 226),
             "https://yt3.googleusercontent.com/x=w226-h226-l90-rj");
    CHECK_EQ(Thumbs::sized_url(g, 100), g + "=w100-h100-l90-rj");
    CHECK_EQ(Thumbs::sized_url("https://lh3.googleusercontent.com/a=b/c", 100),
             "https://lh3.googleusercontent.com/a=b/c=w100-h100-l90-rj");
    CHECK_EQ(Thumbs::sized_url(g + "=w120-h120-l90-rj", 0), g + "=w120-h120-l90-rj");
    CHECK_EQ(Thumbs::sized_url("https://i.ytimg.com/vi_webp/ID/hqdefault.webp?x=1", 0),
             "https://i.ytimg.com/vi/ID/hqdefault.jpg?x=1");
    CHECK_EQ(Thumbs::sized_url("https://i.ytimg.com/vi/ID/hq.jpg", 300), "https://i.ytimg.com/vi/ID/hq.jpg");

    char tmpl[] = "/tmp/bbm_thumbs_XXXXXX";
    const std::string root = mkdtemp(tmpl);
    {   // A cached file is found without the thread fetching anything
        Server srv;
        std::atomic<int> ready{0};
        Thumbs th(root + "/a", [&](const std::string &, int, const std::string &) { ready++; });
        const std::string url = "http://127.0.0.1:" + std::to_string(srv.port) + "/cached.jpg";
        const std::string file = root + "/a/" + sha1_hex(url) + ".jpg";
        { std::ofstream(file, std::ios::binary) << "cached"; }
        auto got = th.request({url, "ftp://x/y.jpg", "file:///etc/passwd"}, 0);
        CHECK_EQ(got.size(), size_t(1));
        CHECK_EQ(got[url], file);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK_EQ(srv.hits.load(), 0);
        CHECK_EQ(ready.load(), 0);
    }
    {   // A real fetch: queued once, reported through ready, then found in the cache
        Server srv;
        std::mutex m;
        std::vector<std::string> files;
        Thumbs th(root + "/b", [&](const std::string &url, int size, const std::string &file) {
            std::lock_guard<std::mutex> lk(m);
            files.push_back(url + "|" + std::to_string(size) + "|" + file);
        });
        const std::string url = "http://127.0.0.1:" + std::to_string(srv.port) + "/pic.jpg";
        CHECK(th.request({url, url}, 0).empty());
        for (int i = 0; i < 500; i++) {
            { std::lock_guard<std::mutex> lk(m); if (!files.empty()) break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::lock_guard<std::mutex> lk(m);
        const std::string file = root + "/b/" + sha1_hex(url) + ".jpg";
        CHECK_EQ(files.size(), size_t(1));
        if (!files.empty()) CHECK_EQ(files[0], url + "|0|" + file);
        CHECK_EQ(srv.hits.load(), 1);
        std::ifstream f(file, std::ios::binary);
        std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK_EQ(body, kBody);
        const auto again = th.request({url}, 0);
        CHECK_EQ(again.size(), size_t(1));
        if (!again.empty()) CHECK_EQ(again.at(url), file);
        CHECK_EQ(srv.hits.load(), 1);
    }
    {   // Two urls of one picture: one fetch, an answer for each
        Server srv;
        std::mutex m;
        std::vector<std::string> urls;
        Thumbs th(root + "/c", [&](const std::string &url, int, const std::string &) {
            std::lock_guard<std::mutex> lk(m);
            urls.push_back(url);
        });
        const std::string base = "http://127.0.0.1:" + std::to_string(srv.port) + "/googleusercontent.com/x";
        const std::string small = base + "=w120-h120-l90-rj", large = base + "=w544-h544-l90-rj";
        CHECK(th.request({small, large}, 226).empty());
        for (int i = 0; i < 500; i++) {
            { std::lock_guard<std::mutex> lk(m); if (urls.size() >= 2) break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::lock_guard<std::mutex> lk(m);
        std::sort(urls.begin(), urls.end());
        CHECK_EQ(urls.size(), size_t(2));
        if (urls.size() == 2) {
            CHECK_EQ(urls[0], small);
            CHECK_EQ(urls[1], large);
        }
        CHECK_EQ(srv.hits.load(), 1);
    }
    return check_done();
}
