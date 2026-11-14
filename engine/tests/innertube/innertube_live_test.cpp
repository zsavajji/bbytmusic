// Against music.youtube.com, anonymously. Run on request: ctest -L live.
#include <cstdio>

#include "check.h"
#include "innertube/next.h"
#include "innertube/search.h"

using namespace bbm;

int main() {
    InnerTube yt;
    std::string err;
    const std::string q = "never gonna give you up";

    struct F { SearchFilter f; const char *name, *kind; };
    for (F x : {F{SearchFilter::Songs, "songs", "song"}, F{SearchFilter::Videos, "videos", "video"},
                F{SearchFilter::Albums, "albums", "album"}, F{SearchFilter::Artists, "artists", "artist"},
                F{SearchFilter::Playlists, "playlists", "playlist"}}) {
        SearchPage p;
        const bool ok = search(yt, q, x.f, p, err);
        if (!ok) std::fprintf(stderr, "%s: %s\n", x.name, err.c_str());
        CHECK(ok);
        size_t n = 0, wrong = 0;
        for (const auto &s : p.sections)
            for (const Item &it : s.items) {
                ++n;
                if (it.kind != x.kind) ++wrong;
            }
        std::printf("filter %-9s %zu items, %zu of another kind, continuation %s\n", x.name, n, wrong,
                    p.continuation.empty() ? "no" : "yes");
        CHECK(n > 0);
        CHECK_EQ(wrong, size_t(0));
    }
    CHECK(!yt.visitor_data().empty());

    SearchPage songs, more, all;
    CHECK(search(yt, q, SearchFilter::Songs, songs, err));
    CHECK(!songs.sections.empty() && !songs.sections[0].items.empty());
    if (!songs.sections.empty() && !songs.sections[0].items.empty()) {
        const Item &it = songs.sections[0].items[0];
        CHECK_EQ(it.title, std::string("Never Gonna Give You Up"));
        CHECK_EQ(it.artist, std::string("Rick Astley"));
        CHECK(it.duration_s >= 210 && it.duration_s <= 216);
        CHECK(!it.id.empty() && !it.thumb.empty());
    }
    CHECK(!songs.continuation.empty());
    const bool more_ok = search_more(yt, songs.continuation, more, err);
    if (!more_ok) std::fprintf(stderr, "search_more: %s\n", err.c_str());
    CHECK(more_ok);
    std::printf("continuation: %zu items\n", more.sections.empty() ? size_t(0) : more.sections[0].items.size());
    CHECK(!more.sections.empty() && more.sections[0].items.size() >= 10);

    CHECK(search(yt, q, SearchFilter::All, all, err));
    CHECK(!all.sections.empty());

    std::vector<std::string> sug;
    CHECK(suggestions(yt, "never gon", sug, err));
    std::printf("suggestions: %zu, first \"%s\"\n", sug.size(), sug.empty() ? "" : sug[0].c_str());
    CHECK(sug.size() >= 3);

    QueuePage qp, qp2;
    const bool qok = watch_queue(yt, "dQw4w9WgXcQ", "", qp, err);
    if (!qok) std::fprintf(stderr, "next: %s\n", err.c_str());
    CHECK(qok);
    std::printf("queue: %zu items, lyrics %s, related %s\n", qp.items.size(), qp.lyrics_browse_id.c_str(),
                qp.related_browse_id.c_str());
    CHECK(qp.items.size() > 10);
    CHECK(!qp.items.empty() && qp.items[0].id == "dQw4w9WgXcQ");
    CHECK(!qp.lyrics_browse_id.empty());
    CHECK(!qp.continuation.empty());
    CHECK(watch_queue_more(yt, qp.continuation, qp2, err));
    std::printf("queue continuation: %zu items\n", qp2.items.size());
    CHECK(qp2.items.size() > 10);
    return check_done();
}
