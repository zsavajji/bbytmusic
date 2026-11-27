.pragma library
// Requests whose failure the page that sent them shows itself: main.qml's one toast for every
// failed command skips these (a song with no lyrics is not an error to toast). Shared between the
// pages and main.qml because it is a library script.
var ids = {};

function add(id) {
    ids[id] = true;
}

// True once for an id that was added; every reply passes through here, so none is kept.
function take(id) {
    if (ids[id]) {
        delete ids[id];
        return true;
    }
    return false;
}
