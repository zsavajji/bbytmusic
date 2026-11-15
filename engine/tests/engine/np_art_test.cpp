// The overlay's cover: copied into a folder of its own, only the latest kept, a failure sends none.
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <sstream>

#include "check.h"
#include "engine/np_art.h"

using namespace bbm;

namespace {

void put(const std::string &p, const std::string &body) { std::ofstream(p, std::ios::binary) << body; }

std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

bool exists(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

}  // namespace

int main() {
    char tmpl[] = "/tmp/bbm_npart_XXXXXX";
    const std::string root = mkdtemp(tmpl);
    put(root + "/a.jpg", "cover a");
    put(root + "/b.jpg", "cover b");

    {   // The folder is made; a second ask for the same file is the same copy
        NpArt art(root + "/shared");
        const std::string a = art.copy(root + "/a.jpg");
        CHECK_EQ(a, root + "/shared/np-a.jpg");
        CHECK_EQ(get(a), std::string("cover a"));
        struct stat st;
        CHECK(::stat(a.c_str(), &st) == 0 && (st.st_mode & 0777) == 0664);
        CHECK_EQ(art.copy(root + "/a.jpg"), a);
        CHECK_EQ(art.copy(""), std::string());

        // A different cover replaces it: only the latest is kept
        const std::string b = art.copy(root + "/b.jpg");
        CHECK_EQ(b, root + "/shared/np-b.jpg");
        CHECK(!exists(a));
        CHECK_EQ(get(b), std::string("cover b"));
    }
    {   // What an earlier run left is cleaned, and only np-*
        put(root + "/shared/np-old.jpg", "x");
        put(root + "/shared/keep.txt", "y");
        NpArt art(root + "/shared");
        art.clean();
        CHECK(!exists(root + "/shared/np-old.jpg"));
        CHECK(!exists(root + "/shared/np-b.jpg"));
        CHECK(exists(root + "/shared/keep.txt"));
    }
    {   // Failures send no cover, and nothing throws: no parent folder, no source file
        NpArt art(root + "/nowhere/deeper");
        CHECK_EQ(art.copy(root + "/a.jpg"), std::string());
        CHECK_EQ(art.copy(root + "/a.jpg"), std::string());
        art.clean();
        NpArt gone(root + "/shared");
        CHECK_EQ(gone.copy(root + "/missing.jpg"), std::string());
        CHECK(!exists(root + "/shared/np-missing.jpg"));
    }
    return check_done();
}
