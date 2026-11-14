// music.youtube.com browse: an album's, a playlist's, an artist's or a feed's page.
#pragma once
#include <string>
#include <vector>

#include "innertube/client.h"
#include "innertube/items.h"
#include "innertube/search.h"

namespace bbm {

struct BrowsePage {
    std::string kind;   // "album", "playlist", "artist" or "feed"
    std::string id, title, subtitle, second_subtitle, artist, artist_id, year, thumb, description, playlist_id;
    std::vector<Item> items;   // songs and videos only (an artist's top songs)
    std::vector<SearchSection> sections;   // an artist's other shelves or a feed's, in page order
    std::string continuation;   // a feed's next shelves ("" at the end)
};

// `id` is an album's browse id (MPRE...) a playlist's (VL...) an artist's (UC...) or a feed's (FE...).
bool browse(InnerTube &, const std::string &id, BrowsePage &out, std::string &err);
// The id `browse` asks for: a bare playlist id (PL…, OLAK…, RDCLAK…, as a search result's play
// button carries it) gets the VL that makes it a page; anything else is left as it is.
std::string browse_id(const std::string &id);
// The parser, on a response already in hand.
bool parse_browse(Json response, const std::string &id, BrowsePage &out, std::string &err);
// A feed's next shelves (`sections` and `continuation` only).
bool browse_more(InnerTube &, const std::string &continuation, BrowsePage &out, std::string &err);
bool parse_browse_more(Json response, BrowsePage &out, std::string &err);

}  // namespace bbm
