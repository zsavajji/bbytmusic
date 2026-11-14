// The Queue page's arithmetic, kept out of the QML so that node can run it
// (app/tests/queue_test.js). Plain ES5: QML's JavaScript is Qt 4.8's.

// How the rows on screen (`old`, the queue's items as last shown) become the engine's queue
// (`now`), as the smallest change the list can take without losing its scroll:
//   {op: "same"}              nothing but perhaps the playing row
//   {op: "append", from: n}   now[n..] are new rows at the end (the radio's next page)
//   {op: "remove", at: k}     row k is gone (Remove)
//   {op: "rebuild"}           anything else (a sort, a shuffle, another queue)
function diff(old, now) {
    var n = old.length;
    var same = 0;
    while (same < n && same < now.length && old[same].id == now[same].id) {
        same ++;
    }
    if (n > 0 && same == n) {
        return now.length == n ? {
            "op": "same"
        } : {
            "op": "append",
            "from": n
        };
    }
    if (n > 1 && now.length == n - 1) {
        for (var k = same; k < now.length; k ++) {
            if (old[k + 1].id != now[k].id) {
                return {
                    "op": "rebuild"
                };
            }
        }
        return {
            "op": "remove",
            "at": same
        };
    }
    return {
        "op": "rebuild"
    };
}

// `list` with the element at `from` taken out and put back at `to`, the index it ends at
// (what ArrayDataModel.move and the engine's queue_move both do). A new array.
function moved(list, from, to) {
    var copy = [];
    for (var i = 0; i < list.length; i ++) {
        copy.push(list[i]);
    }
    var t = copy.splice(from, 1)[0];
    copy.splice(to, 0, t);
    return copy;
}

// Where the row at `index` is after that move.
function indexAfterMove(index, from, to) {
    if (index == from) {
        return to;
    }
    if (from < index && to >= index) {
        return index - 1;
    }
    if (from > index && to <= index) {
        return index + 1;
    }
    return index;
}

// Where a row goes to play right after the current one: queue_move's `to`.
function nextSlot(index, current) {
    return index > current ? current + 1 : current;
}

// How wide a text draws, in units of an ordinary lower-case letter of Slate Pro. Measured on the
// Q10 from a screenshot of the queue (2026-11-14): a unit is about 19.3 px in a list row's
// title and 15.5 px in the line under it, within a few per cent.
function textUnits(text) {
    var units = 0;
    for (var i = 0; i < text.length; i ++) {
        var c = text.charAt(i);
        if ("iljtfrI1.,:;'!| ".indexOf(c) >= 0) {
            units += 0.55;
        } else if ("mwMW".indexOf(c) >= 0) {
            units += 1.5;
        } else if (c != c.toLowerCase()) {
            units += 1.2;
        } else {
            units += 1.0;
        }
    }
    return units;
}

// `text` cut to at most `units` wide, with an ellipsis when it was cut.
// ponytail: an estimate from one phone's font metrics, used so that a StandardListItem's text
// stops short of the rearrange grip; a custom list item with real padding if other screens or
// font sizes make it miss.
function fit(text, units) {
    if (textUnits(text) <= units) {
        return text;
    }
    var out = text;
    while (out.length > 0 && textUnits(out) + 1.0 > units) {
        out = out.substring(0, out.length - 1);
    }
    return out.replace(/[\s•]+$/, "") + "…";
}
