#include <algorithm>

#include "check.h"
#include "player/queue.h"

using namespace bbm;

static Track t(const char *id) { Track x; x.id = id; return x; }
static std::vector<Track> ts(std::initializer_list<const char *> ids) {
    std::vector<Track> v;
    for (auto i : ids) v.push_back(t(i));
    return v;
}
static std::string ids(const Queue &q) {
    std::string s;
    for (auto &x : q.tracks()) s += x.id;
    return s;
}

int main() {
    Queue q;
    CHECK(q.current() == nullptr);
    CHECK_EQ(q.index(), -1);
    CHECK_EQ(q.next_index(), -1);
    CHECK_EQ(q.prev_index(), -1);
    CHECK(!q.remove(0));
    CHECK(!q.move(0, 0));
    CHECK(!q.jump(0));

    q.set(ts({"a", "b", "c", "d"}), 1);
    CHECK_EQ(q.current()->id, std::string("b"));
    CHECK_EQ(q.next_index(), 2);
    CHECK_EQ(q.prev_index(), 0);
    CHECK_EQ(q.remaining(), 2);

    // set clamps the index
    q.set(ts({"a", "b"}), 9);
    CHECK_EQ(q.index(), 1);
    CHECK_EQ(q.next_index(), -1);
    q.set(ts({"a", "b"}), -3);
    CHECK_EQ(q.index(), 0);
    CHECK_EQ(q.prev_index(), -1);
    q.set({}, 0);
    CHECK_EQ(q.index(), -1);

    // add: after the current one, or at the end; the index stays on its track
    q.set(ts({"a", "b", "c"}), 1);
    q.add(ts({"x", "y"}), true);
    CHECK_EQ(ids(q), std::string("abxyc"));
    CHECK_EQ(q.current()->id, std::string("b"));
    q.add(ts({"z"}), false);
    CHECK_EQ(ids(q), std::string("abxyc" "z"));
    CHECK_EQ(q.current()->id, std::string("b"));
    q.add({}, true);
    CHECK_EQ(q.size(), 6);
    Queue e;   // adding to an empty queue selects the first track
    e.add(ts({"p", "q"}), true);
    CHECK_EQ(ids(e), std::string("pq"));
    CHECK_EQ(e.index(), 0);

    // remove: before, after, the current one, the last, the only one
    q.set(ts({"a", "b", "c", "d"}), 2);
    CHECK(q.remove(0));
    CHECK_EQ(q.current()->id, std::string("c"));
    CHECK(q.remove(2));   // d, after
    CHECK_EQ(q.current()->id, std::string("c"));
    CHECK(q.remove(1));   // c itself: the index stays, now on what followed (nothing) -> clamps
    CHECK_EQ(ids(q), std::string("b"));
    CHECK_EQ(q.index(), 0);
    q.set(ts({"a", "b", "c"}), 1);
    CHECK(q.remove(1));   // current with a follower
    CHECK_EQ(q.current()->id, std::string("c"));
    CHECK(q.remove(0));
    CHECK(q.remove(0));
    CHECK_EQ(q.index(), -1);
    CHECK(!q.remove(0));

    // move: the index follows its track whichever way things shift
    q.set(ts({"a", "b", "c", "d", "e"}), 2);   // current c
    CHECK(q.move(2, 4));                        // the current one moves
    CHECK_EQ(ids(q), std::string("abdec"));
    CHECK_EQ(q.current()->id, std::string("c"));
    CHECK(q.move(0, 4));                        // from before to after
    CHECK_EQ(q.current()->id, std::string("c"));
    CHECK(q.move(4, 0));                        // from after to before
    CHECK_EQ(q.current()->id, std::string("c"));
    CHECK(q.move(0, 1));                        // both before: unchanged
    CHECK_EQ(q.current()->id, std::string("c"));
    CHECK(q.move(3, 3));
    CHECK(!q.move(-1, 2));
    CHECK(!q.move(0, 5));
    CHECK_EQ(q.size(), 5);

    CHECK(q.jump(4));
    CHECK_EQ(q.next_index(), -1);
    CHECK(!q.jump(5));
    CHECK_EQ(q.index(), 4);
    // clear_others: the current track only; without one, nothing
    q.set(ts({"a", "b", "c"}), 1);
    q.clear_others();
    CHECK_EQ(ids(q), std::string("b"));
    CHECK_EQ(q.index(), 0);
    q.set({}, 0);
    q.clear_others();
    CHECK_EQ(q.size(), 0);
    CHECK_EQ(q.index(), -1);

    // shuffle_after_current: the front stays, the same tracks come back
    std::mt19937 rng(7);
    q.set(ts({"a", "b", "c", "d", "e", "f", "g", "h"}), 2);
    q.shuffle_after_current(rng);
    CHECK_EQ(ids(q).substr(0, 3), std::string("abc"));
    CHECK_EQ(q.index(), 2);
    std::string rest = ids(q).substr(3);
    std::sort(rest.begin(), rest.end());
    CHECK_EQ(rest, std::string("defgh"));
    q.set(ts({"a", "b", "c", "d", "e", "f", "g", "h"}), -1);   // clamped to 0
    q.shuffle_after_current(rng);
    CHECK_EQ(q.current()->id, std::string("a"));

    // sort: stable, case-blind for text, the index follows the current track (even among duplicates)
    {
        auto tk = [](const char *id, const char *title, const char *artist, const char *album, int dur) {
            Track x; x.id = id; x.title = title; x.artist = artist; x.album = album; x.duration_s = dur; return x;
        };
        auto fresh = [&] {
            q.set({tk("1", "banana", "Zed", "Y", 30), tk("2", "Apple", "amy", "X", 10), tk("3", "cherry", "Zed", "X", 20),
                   tk("2", "Apple", "amy", "X", 10)}, 3);   // the current one is the second "2"
        };
        fresh();
        q.sort(QueueSort::Title, false);
        CHECK_EQ(ids(q), std::string("2213"));
        CHECK_EQ(q.index(), 1);   // the second Apple, after the first
        q.sort(QueueSort::Title, true);
        CHECK_EQ(ids(q), std::string("3122"));
        CHECK_EQ(q.index(), 3);
        fresh();
        q.sort(QueueSort::Artist, false);
        CHECK_EQ(ids(q), std::string("2213"));   // amy, amy, then Zed in order
        CHECK_EQ(q.index(), 1);
        fresh();
        q.sort(QueueSort::Album, false);
        CHECK_EQ(ids(q), std::string("2321"));
        CHECK_EQ(q.index(), 2);
        fresh();
        q.sort(QueueSort::Duration, false);
        CHECK_EQ(ids(q), std::string("2231"));
        q.sort(QueueSort::Duration, true);
        CHECK_EQ(ids(q), std::string("1322"));
        CHECK_EQ(q.index(), 3);
        q.set({}, 0);
        q.sort(QueueSort::Title, false);
        CHECK_EQ(q.index(), -1);
    }

    // repeat: next_index is a manual Next, auto_next_index what follows a track that ended
    q.set(ts({"a", "b", "c"}), 1);
    CHECK(q.repeat() == Repeat::Off);
    CHECK_EQ(q.next_index(), 2);
    CHECK_EQ(q.auto_next_index(), 2);
    q.jump(2);
    CHECK_EQ(q.next_index(), -1);
    CHECK_EQ(q.auto_next_index(), -1);
    q.set_repeat(Repeat::All);
    CHECK_EQ(q.next_index(), 0);
    CHECK_EQ(q.auto_next_index(), 0);
    q.jump(1);
    CHECK_EQ(q.next_index(), 2);
    CHECK_EQ(q.auto_next_index(), 2);
    q.set_repeat(Repeat::One);
    CHECK_EQ(q.next_index(), 2);
    CHECK_EQ(q.auto_next_index(), 1);
    q.jump(2);
    CHECK_EQ(q.next_index(), -1);   // Next still moves on, and stops at the end
    CHECK_EQ(q.auto_next_index(), 2);
    q.set({}, 0);
    CHECK_EQ(q.auto_next_index(), -1);   // nothing to repeat
    q.set(ts({"a"}), 0);
    q.set_repeat(Repeat::All);
    CHECK_EQ(q.next_index(), 0);
    return check_done();
}
