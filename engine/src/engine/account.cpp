#include "engine/account.h"

#include "util/json.h"
#include "util/log.h"

namespace bbm {

std::string Account::stored_locked() const {
    if (refresh_.empty()) return "";
    return "{\"refresh_token\":" + json_quote(refresh_) + ",\"client_id\":" + json_quote(client_.id) + ",\"client_secret\":" +
           json_quote(client_.secret) + ",\"source\":" + json_quote(source_) + ",\"name\":" + json_quote(name_) + ",\"photo\":" +
           json_quote(photo_) + "}";
}

std::string Account::stored() const {
    std::lock_guard<std::mutex> lk(mu_);
    return stored_locked();
}

Account::Info Account::info() const {
    std::lock_guard<std::mutex> lk(mu_);
    return {!refresh_.empty(), name_, photo_, source_};
}

void Account::load(const std::string &stored) {
    std::lock_guard<std::mutex> lk(mu_);
    refresh_.clear();
    access_.clear();
    JsonDoc doc;
    std::string err;
    if (stored.empty() || !JsonDoc::parse(stored, doc, err)) return;
    const Json j = doc.root();
    if (!j["refresh_token"].is_string() || j["refresh_token"].str().empty() || j["client_id"].str().empty() ||
        j["client_secret"].str().empty())
        return;
    refresh_ = j["refresh_token"].string();
    client_ = {j["client_id"].string(), j["client_secret"].string()};
    source_ = j["source"].string();
    name_ = j["name"].string();
    photo_ = j["photo"].string();
}

OAuthResult Account::import(const std::string &refresh, OAuthClient client, const std::string &source, std::string &err) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (client.id.empty() && client.secret.empty() && !fetch_tv_client(send_, client, err)) return OAuthResult::Network;
        OAuthToken tok;
        if (const OAuthResult r = refresh_token(send_, client, refresh, tok, err); r != OAuthResult::Ok) return r;
        AccountInfo who;
        std::string e;
        const OAuthResult r = fetch_account(send_, tok.access, who, e);
        if (r == OAuthResult::Refused) {
            err = e;
            return r;
        }
        if (r == OAuthResult::Network) log_warn("account: name not fetched: " + e);
        refresh_ = tok.refresh.empty() ? refresh : tok.refresh;
        client_ = client;
        source_ = source;
        name_ = who.name;
        photo_ = who.photo;
        access_ = tok.access;
        expires_ = std::chrono::steady_clock::now() + std::chrono::seconds(tok.expires_in);
    }
    changed();
    return OAuthResult::Ok;
}

void Account::sign_out() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (refresh_.empty()) return;
        refresh_.clear();
        access_.clear();
        name_.clear();
        photo_.clear();
        source_.clear();
    }
    changed();
}

OAuthResult Account::bearer(std::string &out, std::string &err) {
    bool did_change = false;
    OAuthResult res = OAuthResult::Ok;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (refresh_.empty()) {
            err = "not signed in";
            return OAuthResult::Refused;
        }
        if (access_.empty() || expires_ - std::chrono::steady_clock::now() < std::chrono::seconds(60)) {
            OAuthToken tok;
            res = refresh_token(send_, client_, refresh_, tok, err);
            if (res == OAuthResult::Ok) {
                access_ = tok.access;
                expires_ = std::chrono::steady_clock::now() + std::chrono::seconds(tok.expires_in);
                if (!tok.refresh.empty() && tok.refresh != refresh_) {
                    refresh_ = tok.refresh;
                    did_change = true;
                }
            } else if (res == OAuthResult::Refused) {
                log_warn("account: refresh refused");
                refresh_.clear();
                access_.clear();
                name_.clear();
                photo_.clear();
                source_.clear();
                did_change = true;
            }
        }
        if (res == OAuthResult::Ok) out = access_;
    }
    if (did_change) changed();
    return res;
}

}  // namespace bbm
