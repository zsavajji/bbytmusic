// The country YouTube sees this client in: music.youtube.com/sw.js_data, the service worker's
// data, names it. No third-party geolocation service.
#pragma once
#include <string>
#include <string_view>

#include "innertube/client.h"

namespace bbm {

// A `gl`: two uppercase ASCII letters.
bool valid_region(std::string_view);

// The country in a sw.js_data body: )]}' and a newline, then JSON whose root[0][2][0][0] is
// ["it","IT",null,"<ip>",…]; index 1 is the country.
bool parse_sw_data(const std::string &body, std::string &gl, std::string &err);

// GET https://music.youtube.com/sw.js_data and parse it. Gives up after a few seconds.
bool fetch_region(InnerTube &, std::string &gl, std::string &err);

}  // namespace bbm
