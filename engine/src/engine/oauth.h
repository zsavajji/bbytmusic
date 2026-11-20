// YouTube TV's OAuth: the app's own client (read from its page, as SmartTube does), a refresh
// token turned into an access token, and the name of the account it belongs to. No Engine here.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "net/http.h"

namespace bbm {

using HttpSendFn = std::function<HttpResponse(const HttpRequest &)>;

struct OAuthClient {
    std::string id, secret;
};
struct OAuthToken {
    std::string access, refresh;   // refresh is "" when the answer carries none (the old one stays good)
    int64_t expires_in = 0;
};
struct AccountInfo {
    std::string name, photo;
};

// The TV page's script, as an absolute url ("" if the page names none).
std::string tv_base_js_url(std::string_view tv_page);
// The first `clientId:"<id>.apps.googleusercontent.com",<name>:"<secret>"` in that script.
bool parse_tv_client(std::string_view base_js, OAuthClient &out);
bool fetch_tv_client(const HttpSendFn &send, OAuthClient &out, std::string &err);

// The token endpoint's answer. {"error":"x"} is false with err "x" exactly.
bool parse_token(std::string_view body, OAuthToken &out, std::string &err);

// Network: no answer, a 5xx or an answer that makes no sense; Refused: Google says no.
enum class OAuthResult { Ok, Network, Refused };
OAuthResult refresh_token(const HttpSendFn &send, const OAuthClient &c, const std::string &refresh, OAuthToken &out, std::string &err);

// The selected account of an accounts_list answer (the first when none is marked).
bool parse_account(std::string_view body, AccountInfo &out);
OAuthResult fetch_account(const HttpSendFn &send, const std::string &access, AccountInfo &out, std::string &err);

}  // namespace bbm
