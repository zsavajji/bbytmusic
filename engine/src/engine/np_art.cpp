#include "engine/np_art.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "util/log.h"

namespace bbm {
namespace {

// "" when `from` is copied to `to` (the folder made if need be); else the step that failed and why.
std::string copy_into(const std::string &dir, const std::string &from, const std::string &to) {
    auto why = [](const char *step) { return std::string(step) + ": " + std::strerror(errno); };
    // The setgid parent gives the folder its group (1000_shared); no chmod, it could clear that bit.
    if (::mkdir(dir.c_str(), 0775) != 0 && errno != EEXIST) return why("mkdir");
    std::ifstream in(from, std::ios::binary);
    if (!in) return why("open the cover");
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) return why("create the copy");
    out << in.rdbuf();
    out.close();
    if (!out) {
        const std::string e = why("write the copy");
        std::remove(to.c_str());
        return e;
    }
    ::chmod(to.c_str(), 0664);   // the umask may have taken the group's write off
    return "";
}

}  // namespace

void NpArt::clean() {
    DIR *d = ::opendir(dir_.c_str());
    if (!d) return;
    while (dirent *e = ::readdir(d))
        if (std::strncmp(e->d_name, "np-", 3) == 0) std::remove((dir_ + "/" + e->d_name).c_str());
    ::closedir(d);
}

std::string NpArt::copy(const std::string &src) {
    if (src.empty()) return "";
    if (src == src_) return dst_;
    const std::string dst = dir_ + "/np-" + src.substr(src.rfind('/') + 1);
    if (const std::string err = copy_into(dir_, src, dst); !err.empty()) {
        if (warned_.insert(err).second) log_warn("now-playing: no cover for the overlay, " + err + " (" + dir_ + ")");
        return "";
    }
    if (!dst_.empty() && dst_ != dst) std::remove(dst_.c_str());
    src_ = src;
    dst_ = dst;
    return dst;
}

}  // namespace bbm
