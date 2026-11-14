// music.youtube.com browse: an album's, a playlist's or an artist's page.
#pragma once
#include <string>
#include <vector>

#include "innertube/client.h"
#include "innertube/items.h"
#include "innertube/search.h"

namespace bbm {

struct BrowsePage {
    std::string kind;   // "album", "playlist" or "artist"
    std::string id, title, subtitle, second_subtitle, artist, artist_id, year, thumb, description, playlist_id;
    std::vector<Item> items;   // songs and videos only (an artist's top songs)
    std::vector<SearchSection> sections;   // an artist's other shelves, in page order
};

// `id` is an album's browse id (MPRE...) a playlist's (VL...) or an artist's (UC...).
bool browse(InnerTube &, const std::string &id, BrowsePage &out, std::string &err);
// The parser, on a response already in hand.
bool parse_browse(Json response, const std::string &id, BrowsePage &out, std::string &err);

}  // namespace bbm
