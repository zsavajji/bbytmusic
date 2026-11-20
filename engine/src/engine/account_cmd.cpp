// Engine: the `account` command. It runs on the reader thread of its connection, network included.
#include "engine/engine.h"
#include "util/log.h"

namespace bbm {

std::string Engine::account_json() {
    const Account::Info i = account_->info();
    return std::string("\"state\":") + (i.in ? "\"in\"" : "\"out\"") + ",\"name\":" + json_quote(i.name) + ",\"photo\":" +
           json_quote(i.photo) + ",\"source\":" + json_quote(i.source);
}

Reply Engine::account_command(Json req) {
    auto bad = [](const char *why) { return Reply::error("bad_request", why); };
    const bool out = req["sign_out"].boolean();
    if (!req["import"].is_null() && out) return bad("import and sign_out: one at a time");
    if (!req["import"].is_null()) {
        const Json im = req["import"];
        if (!im.is_object()) return bad("import: an object is required");
        if (!im["refresh_token"].is_string() || im["refresh_token"].str().empty()) return bad("import.refresh_token: a string is required");
        const Json id = im["client_id"], secret = im["client_secret"];
        const bool has_id = !id.is_null(), has_secret = !secret.is_null();
        if (has_id != has_secret || (has_id && (!id.is_string() || id.str().empty() || !secret.is_string() || secret.str().empty())))
            return bad("import: client_id and client_secret together, non-empty strings");
        std::string err;
        const OAuthResult r = account_->import(im["refresh_token"].string(), {id.string(), secret.string()}, "tube", err);
        if (r == OAuthResult::Network) return Reply::error("network", err);
        if (r == OAuthResult::Refused) return Reply::error("refused", err);
        log_info("account: signed in (tube)");
    } else if (out) {
        if (account_->info().in) log_info("account: signed out");
        account_->sign_out();
    }
    Reply r;
    r.result = "{" + account_json() + "}";
    return r;
}

}  // namespace bbm
