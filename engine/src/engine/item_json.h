// The JSON of docs/protocol.md: items, tracks, states; and the requests' items read back.
#pragma once
#include <string>
#include <vector>

#include "innertube/items.h"
#include "innertube/search.h"
#include "player/player.h"
#include "player/track.h"
#include "store/track_store.h"
#include "util/json.h"

namespace bbm {

// "stopped" "loading" "playing" "paused"
const char *state_name(PlayState);

// The protocol's Item: as innertube's to_json(), but the flag is "explicit".
std::string item_json(const Item &);
std::string search_page_json(const SearchPage &);

// A request's "items": every element an object with a kind and an id. False with `why` otherwise,
// or when one is of a kind that cannot be played (only songs and videos can).
bool parse_items(Json array, std::vector<Item> &out, std::string &why);
bool playable(const Item &);

Track to_track(const Item &);
TrackRow to_row(const Item &);
Item from_row(const TrackRow &);       // a restored track: kind "song", subtitle rebuilt
Item from_track(const Track &);        // a track nothing more is known of

}  // namespace bbm
