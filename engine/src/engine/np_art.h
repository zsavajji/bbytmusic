// The cover for the system's now-playing overlay. The overlay is another process
// (/usr/sbin/nowplaying, uid 393, group 1000_shared) and cannot read our sandbox, so the cover
// is copied into a folder of the shared storage, which that group can read.
#pragma once
#include <set>
#include <string>
#include <utility>

namespace bbm {

class NpArt {
public:
    explicit NpArt(std::string dir) : dir_(std::move(dir)) {}

    void clean();   // removes the np-* files an earlier run left; best effort

    // The copy of the cache file `src` in the folder (made on the first call for a file), or "" if
    // there is none: no src, or the copy failed (warned once per kind of failure). The copy made
    // before is removed when a different one is made. One thread (the engine's worker).
    std::string copy(const std::string &src);

private:
    std::string dir_;
    std::string src_, dst_;       // the file last copied, and its copy
    std::set<std::string> warned_;
};

}  // namespace bbm
