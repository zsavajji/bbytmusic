// music.youtube.com browse: an album's or a playlist's header and tracks.
#pragma once
#include <string>
#include <vector>

#include "innertube/client.h"
#include "innertube/items.h"

namespace bbm {

struct BrowsePage {
    std::string kind;   // "album" or "playlist"
    std::string id, title, subtitle, second_subtitle, artist, artist_id, year, thumb, description, playlist_id;
    std::vector<Item> items;   // songs and videos only
};

// `id` is an album's browse id (MPRE...) or a playlist's (VL...).
bool browse(InnerTube &, const std::string &id, BrowsePage &out, std::string &err);
// The parser, on a response already in hand.
bool parse_browse(Json response, const std::string &id, BrowsePage &out, std::string &err);

}  // namespace bbm
