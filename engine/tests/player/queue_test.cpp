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
    return check_done();
}
