// Engine: the `account` command. It runs on the reader thread of its connection, network included.
#include "engine/engine.h"
#include "util/log.h"

namespace bbm {

std::string Engine::account_json() {
    const Account::Info i = account_->info();
    return std::string("\"state\":") + (i.pending ? "\"pending\"" : i.in ? "\"in\"" : "\"out\"") + ",\"name\":" + json_quote(i.name) +
           ",\"photo\":" + json_quote(i.photo) + ",\"source\":" + json_quote(i.source) + ",\"code\":" + json_quote(i.code) +
           ",\"url\":" + json_quote(i.url) + ",\"error\":" + json_quote(i.error);
}

Reply Engine::account_command(Json req) {
    auto bad = [](const char *why) { return Reply::error("bad_request", why); };
    const bool out = req["sign_out"].boolean(), sign_in = req["sign_in"].boolean(), cancel = req["cancel"].boolean();
    if (int(!req["import"].is_null()) + out + sign_in + cancel > 1) return bad("import, sign_in, cancel and sign_out: one at a time");
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
    } else if (sign_in) {
        account_->sign_in();
        log_info("account: sign-in started");
    } else if (cancel) {
        account_->cancel_sign_in();
    } else if (out) {
        if (account_->info().in) log_info("account: signed out");
        const std::string t = account_->sign_out();
        if (!t.empty()) post([this, t] { account_->revoke(t); });   // the worker does it: the reply does not wait for the network
    }
    Reply r;
    r.result = "{" + account_json() + "}";
    return r;
}

}  // namespace bbm
