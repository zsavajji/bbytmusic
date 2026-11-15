// music.youtube.com search: filters, continuation, suggestions.
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "innertube/client.h"
#include "innertube/items.h"

namespace bbm {

struct SearchSection { std::string title; std::vector<Item> items; };
struct SearchPage { std::vector<SearchSection> sections; std::string continuation; };

enum class SearchFilter { All, Songs, Videos, Albums, Artists, Playlists, Podcasts };

// The `params` of a filter ("" for All), as the search page's own filter chips carry them.
std::string_view search_params(SearchFilter);

// A filtered search returns one section holding that kind (plus, as YouTube sees fit, a top
// result); All returns a mix. `continuation` is empty when there is no next page.
bool search(InnerTube &, std::string_view query, SearchFilter filter, SearchPage &out, std::string &err);
bool search_more(InnerTube &, const std::string &continuation, SearchPage &out, std::string &err);

// music/get_search_suggestions: the completions of a typed prefix, best first.
bool suggestions(InnerTube &, std::string_view prefix, std::vector<std::string> &out, std::string &err);

// The parsers, on a response already in hand (the tests call them on recorded files).
bool parse_search(Json response, SearchPage &out, std::string &err);
bool parse_suggestions(Json response, std::vector<std::string> &out);

}  // namespace bbm
