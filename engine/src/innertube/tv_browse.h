// The pages YouTube's TV client (TVHTML5) returns for a signed-in account's library: a tab's tiles,
// a playlist's page, and the continuations of both.
#pragma once
#include <string>
#include <vector>

#include "innertube/items.h"

namespace bbm {

struct TvTab { std::string id, title; bool selected = false; };   // id: a browse id (FEmusic_liked_playlists, ...)
struct TvPage {
    std::string title, subtitle;   // a playlist's name and byline; "" on a library tab and on a continuation
    std::vector<TvTab> tabs;       // the library's tabs, in order; empty on a playlist and on a continuation
    std::vector<Item> items;
    std::string continuation;      // "" at the end
};

// One tileRenderer (the object inside {"tileRenderer": ...}). False, leaving `out` untouched, for a
// tile the app does not show (a Short, a link to a feed, a tile with no known type).
bool parse_tile(Json tile, Item &out);
// A library tab's page, a playlist's page, or a continuation of either. False when the answer is
// none of them (no "contents" and no "continuationContents").
bool parse_tv_page(Json root, TvPage &out);

}  // namespace bbm
