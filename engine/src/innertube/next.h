// music.youtube.com `next`: the watch queue and the song radio.
#pragma once
#include <string>
#include <vector>

#include "innertube/client.h"
#include "innertube/items.h"

namespace bbm {

struct QueuePage {
    std::vector<Item> items;
    std::string continuation;                       // "" at the end
    std::string lyrics_browse_id, related_browse_id;   // "" if the song has none; first page only
};

// With an empty playlist_id this asks for the song's radio (RDAMVM<video_id>): the result starts
// with the song itself, followed by what YouTube Music would play next.
bool watch_queue(InnerTube &, const std::string &video_id, const std::string &playlist_id, QueuePage &out,
                 std::string &err);
bool watch_queue_more(InnerTube &, const std::string &continuation, QueuePage &out, std::string &err);

// The parser, on a response already in hand.
bool parse_queue(Json response, QueuePage &out, std::string &err);

}  // namespace bbm
