// The JSON of docs/protocol.md: items, tracks, states; and the requests' items read back.
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "innertube/browse.h"
#include "innertube/items.h"
#include "innertube/search.h"
#include "player/player.h"
#include "player/track.h"
#include "store/history_store.h"
#include "store/track_store.h"
#include "util/json.h"

namespace bbm {

// "stopped" "loading" "playing" "paused"
const char *state_name(PlayState);
// "off" "all" "one"
const char *repeat_name(Repeat);
bool parse_repeat(std::string_view, Repeat &);

// The protocol's Item: as innertube's to_json(), but the flag is "explicit".
std::string item_json(const Item &);
std::string search_page_json(const SearchPage &);
std::string browse_page_json(const BrowsePage &);
std::string browse_more_json(const BrowsePage &);
// A track of the history as an Item (from_row) plus "played_at".
std::string played_json(const PlayedTrack &);

// A request's "items": every element an object with a kind and an id. False with `why` otherwise,
// or when one is of a kind that cannot be played (only songs, videos and episodes can).
bool parse_items(Json array, std::vector<Item> &out, std::string &why);
bool playable(const Item &);

Track to_track(const Item &);
TrackRow to_row(const Item &);
Item from_row(const TrackRow &);       // a restored track: kind "song", subtitle rebuilt
Item from_track(const Track &);        // a track nothing more is known of

}  // namespace bbm
