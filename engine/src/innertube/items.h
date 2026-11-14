// The renderers a search and a queue return, reduced to one struct. All the UI ever sees of a
// renderer is an Item.
#pragma once
#include <string>
#include <string_view>

#include "util/json.h"

namespace bbm {

struct Item {
    std::string kind;        // "song" "video" "album" "artist" "playlist" "station"
    std::string id;          // videoId, or a browse id for the other kinds
    std::string title, subtitle, thumb;   // subtitle: "Artist • Album" as YouTube gives it
    std::string artist, album, artist_id, album_id, playlist_id, params;
    int duration_s = 0; bool is_explicit = false;
};

// `renderer_parent` is the object that holds the renderer under its own name, e.g. an element of
// a shelf's "contents": {"musicResponsiveListItemRenderer": {...}}. Understands
// musicResponsiveListItemRenderer, musicTwoRowItemRenderer, playlistPanelVideoRenderer (also
// inside playlistPanelVideoWrapperRenderer) and musicCardShelfRenderer (its top result).
// Returns false, leaving `out` untouched, for anything else, and for what has no kind or no id
// (podcast episodes, headers, messages).
bool parse_item(Json renderer_parent, Item &out);

// {"kind":"song","id":"...",...}: every field, strings through json_quote, duration_s as a
// number and is_explicit as a bool.
std::string to_json(const Item &);

// ---- helpers, shared with search.cpp and next.cpp ----
// The concatenated text of a {"runs":[{"text":..}]} (or {"simpleText":..}) object.
std::string runs_text(Json text_obj);
// "3:34" -> 214, "1:02:03" -> 3723; 0 if the text is not a duration.
int parse_duration(std::string_view text);

}  // namespace bbm
