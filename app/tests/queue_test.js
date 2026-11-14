// node app/tests/queue_test.js — checks app/assets/queue.js, the Queue page's arithmetic.
var fs = require("fs"), path = require("path"), vm = require("vm"), assert = require("assert");
var Q = {};
vm.runInNewContext(fs.readFileSync(path.join(__dirname, "..", "assets", "queue.js"), "utf8"), Q);

function ids(s) {   // "abc" -> [{id:"a"},{id:"b"},{id:"c"}]
    return s.split("").map(function (c) { return { id: c }; });
}
function str(list) {
    return list.map(function (t) { return t.id; }).join("");
}
function same(a, b) {   // across the vm's context, deepStrictEqual trips on prototypes
    assert.strictEqual(JSON.stringify(a), JSON.stringify(b));
}

// diff
same(Q.diff(ids("abc"), ids("abc")), { op: "same" });
same(Q.diff(ids("abc"), ids("abcde")), { op: "append", from: 3 });
same(Q.diff(ids("abcd"), ids("bcd")), { op: "remove", at: 0 });
same(Q.diff(ids("abcd"), ids("acd")), { op: "remove", at: 1 });
same(Q.diff(ids("abcd"), ids("abc")), { op: "remove", at: 3 });
same(Q.diff(ids("abab"), ids("aab")), { op: "remove", at: 1 });      // duplicates
same(Q.diff(ids("abcd"), ids("dcba")), { op: "rebuild" });
same(Q.diff(ids("abcd"), ids("abd" + "x").slice(0, 3).concat(ids("z"))), { op: "rebuild" });
same(Q.diff(ids("abcd"), ids("xyz")), { op: "rebuild" });
same(Q.diff(ids(""), ids("abc")), { op: "rebuild" });                // the first fill
same(Q.diff(ids("abc"), ids("")), { op: "rebuild" });
same(Q.diff(ids("a"), ids("")), { op: "rebuild" });                  // nothing left to keep a scroll for
same(Q.diff(ids("ab"), ids("a")), { op: "remove", at: 1 });
same(Q.diff(ids("abc"), ids("abd")), { op: "rebuild" });

// moved: the element ends at `to`, as Queue::move (engine/src/player/queue.cpp) has it
assert.strictEqual(str(Q.moved(ids("abcdef"), 5, 2)), "abfcde");
assert.strictEqual(str(Q.moved(ids("abcdef"), 2, 5)), "abdefc");
assert.strictEqual(str(Q.moved(ids("abcdef"), 0, 0)), "abcdef");
var before = ids("abc");
Q.moved(before, 0, 2);
assert.strictEqual(str(before), "abc");                              // a copy, not in place

// indexAfterMove agrees with moved, for every row of a list and every move
var list = ids("abcdefg");
for (var from = 0; from < list.length; from ++) {
    for (var to = 0; to < list.length; to ++) {
        var after = Q.moved(list, from, to);
        for (var i = 0; i < list.length; i ++) {
            assert.strictEqual(after[Q.indexAfterMove(i, from, to)].id, list[i].id,
                "row " + i + " after " + from + " -> " + to);
        }
    }
}

// nextSlot: after the move the row sits right behind the playing one
for (var cur = 0; cur < list.length; cur ++) {
    for (var row = 0; row < list.length; row ++) {
        if (row == cur) {
            continue;
        }
        var to2 = Q.nextSlot(row, cur);
        var moved2 = Q.moved(list, row, to2);
        var curNow = Q.indexAfterMove(cur, row, to2);
        assert.strictEqual(moved2[curNow].id, list[cur].id);
        assert.strictEqual(moved2[curNow + 1].id, list[row].id, "row " + row + " next after " + cur);
    }
}
console.log("queue.js: ok");
