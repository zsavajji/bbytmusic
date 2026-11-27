// YouTube Music's own lyrics tab: plain text and a credit, from the browse id `next` gives.
#pragma once
#include <string>

#include "innertube/client.h"

namespace bbm {

// The parser, on a browse response already in hand. A response without the text (YouTube answers
// a message when the song has no lyrics) is true with `text` empty.
bool parse_yt_lyrics(Json response, std::string &text, std::string &credit, std::string &err);

// `next` for the browse id of the lyrics tab, then `browse` on it. A song without the tab is true
// with `text` empty. `credit` is YouTube's footer ("Source: Musixmatch").
bool yt_lyrics(InnerTube &, const std::string &video_id, std::string &text, std::string &credit, std::string &err);

}  // namespace bbm
