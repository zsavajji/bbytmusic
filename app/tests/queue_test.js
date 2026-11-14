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
// fit: the widths measured on the Q10 screenshot (px = units x 19.3 for a title, x 15.5 under it)
var rows = [["3:29 \u2022 Modest Mouse \u2022 Good News For People", 602, 15.5],
    ["4:05 \u2022 Neutral Milk Hotel \u2022 On Avery Island", 552, 15.5],
    ["Where You'll Find Me Now", 435, 19.3], ["Jigsaw Falling Into Place", 399, 19.3]];
rows.forEach(function (r) {
    var px = Q.textUnits(r[0]) * r[2];
    assert.ok(Math.abs(px - r[1]) / r[1] < 0.07, r[0] + ": " + px + " px estimated, " + r[1] + " measured");
});
assert.strictEqual(Q.fit("Float On", 26), "Float On");
var cut = Q.fit("3:29 \u2022 Modest Mouse \u2022 Good News For People Who Love Bad News", 34);
assert.ok(Q.textUnits(cut) <= 34, cut);
assert.strictEqual(cut.charAt(cut.length - 1), "\u2026");
assert.strictEqual(cut.indexOf("3:29 \u2022 Modest Mouse \u2022 Good News"), 0, cut);
assert.strictEqual(Q.fit("", 10), "");
assert.strictEqual(Q.fit("WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", 3), "W\u2026");
if (process.argv[2] == "--show") {      // where each of the screenshot's rows would end, in px
    [["3:29 \u2022 Modest Mouse \u2022 Good News For People Who Love Bad News", 34, 15.5],
     ["4:05 \u2022 Neutral Milk Hotel \u2022 On Avery Island", 34, 15.5],
     ["1:27 \u2022 Richy Mitch & The Coal Miners \u2022 RMCM", 34, 15.5],
     ["Where You'll Find Me Now", 26, 19.3],
     ["The King of Carrot Flowers Pts. Two & Three", 26, 19.3]].forEach(function (r) {
        var c = Q.fit(r[0], r[1]);
        console.log(c + "  ->  ends near x " + Math.round(98 + Q.textUnits(c) * r[2]));
    });
}
console.log("queue.js: ok");
