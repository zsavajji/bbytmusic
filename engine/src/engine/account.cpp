#include "engine/account.h"

#include <algorithm>
#include <random>

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
    return {!refresh_.empty(), name_, photo_, source_, pending_, code_, url_, error_};
}

bool Account::credentials(std::string &refresh, OAuthClient &client) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (refresh_.empty()) return false;
    refresh = refresh_;
    client = client_;
    return true;
}

Account::~Account() { halt(); }

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

void Account::adopt_locked(const std::string &refresh, const OAuthClient &client, const std::string &source, const AccountInfo &who,
                           const OAuthToken &tok) {
    refresh_ = tok.refresh.empty() ? refresh : tok.refresh;
    client_ = client;
    source_ = source;
    name_ = who.name;
    photo_ = who.photo;
    access_ = tok.access;
    expires_ = std::chrono::steady_clock::now() + std::chrono::seconds(tok.expires_in);
    error_.clear();
}

OAuthResult Account::import(const std::string &refresh, OAuthClient client, const std::string &source, std::string &err) {
    if (halt()) changed();
    {
        std::lock_guard<std::mutex> net(net_mu_);
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
        std::lock_guard<std::mutex> lk(mu_);
        adopt_locked(refresh, client, source, who, tok);
    }
    changed();
    return OAuthResult::Ok;
}

std::string Account::sign_out() {
    const bool was_pending = halt();
    std::string token;
    bool did_change = was_pending;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!error_.empty()) did_change = true;
        if (!refresh_.empty()) {
            did_change = true;
            token = refresh_;   // ours or Tube's: a sign-out here ends both (the user, 2026-11-21)
        }
        refresh_.clear();
        access_.clear();
        name_.clear();
        photo_.clear();
        source_.clear();
        error_.clear();
    }
    if (did_change) changed();
    return token;
}

void Account::revoke(const std::string &token) {
    std::string err;
    std::lock_guard<std::mutex> net(net_mu_);
    if (!revoke_token(send_, token, err)) log_warn("account: revoke failed: " + err);
}

void Account::invalidate() {
    std::lock_guard<std::mutex> lk(mu_);
    access_.clear();
}

OAuthResult Account::bearer(std::string &out, std::string &err) {
    auto fresh = [&] { return !access_.empty() && expires_ - std::chrono::steady_clock::now() >= std::chrono::seconds(60); };
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (refresh_.empty()) {
            err = "not signed in";
            return OAuthResult::Refused;
        }
    }
    std::lock_guard<std::mutex> net(net_mu_);
    std::string refresh;
    OAuthClient client;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (refresh_.empty()) {
            err = "not signed in";
            return OAuthResult::Refused;
        }
        if (fresh()) {
            out = access_;
            return OAuthResult::Ok;
        }
        refresh = refresh_;
        client = client_;
    }
    OAuthToken tok;
    const OAuthResult res = refresh_token(send_, client, refresh, tok, err);
    bool did_change = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (refresh_ != refresh) {   // the account changed during the call: this answer is not for it
            err = "account changed";
            return OAuthResult::Network;
        }
        if (res == OAuthResult::Ok) {
            access_ = tok.access;
            expires_ = std::chrono::steady_clock::now() + std::chrono::seconds(tok.expires_in);
            if (!tok.refresh.empty() && tok.refresh != refresh_) {
                refresh_ = tok.refresh;
                did_change = true;
            }
            out = access_;
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
    if (did_change) changed();
    return res;
}

// The sign-in thread.

bool Account::halt() {
    std::lock_guard<std::mutex> th(th_mu_);
    return halt_locked();
}

bool Account::halt_locked() {
    stop_ = true;
    {
        std::lock_guard<std::mutex> lk(mu_);   // so that a wait that checked stop_ is asleep before the notify
    }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
    stop_ = false;
    std::lock_guard<std::mutex> lk(mu_);
    if (!pending_) return false;
    pending_ = false;
    code_.clear();
    url_.clear();
    return true;
}

void Account::sign_in() {
    std::lock_guard<std::mutex> th(th_mu_);
    halt_locked();   // a sign-in already in flight is replaced
    {
        std::lock_guard<std::mutex> lk(mu_);
        pending_ = true;
        code_.clear();
        url_.clear();
        error_.clear();
    }
    changed();
    th_ = std::thread([this] { sign_in_run(); });
}

void Account::cancel_sign_in() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!pending_) return;
    }
    if (halt()) changed();
}

void Account::end_sign_in(const std::string &err) {
    if (stop_) return;   // whoever stopped it sets the state
    {
        std::lock_guard<std::mutex> lk(mu_);
        pending_ = false;
        code_.clear();
        url_.clear();
        error_ = err;
    }
    changed();
}

namespace {

std::string random_device_id() {   // shaped like a uuid
    std::random_device rd;
    std::string s;
    static const char *hex = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        if (i == 8 || i == 12 || i == 16 || i == 20) s += '-';
        s += hex[rd() & 15];
    }
    return s;
}

}  // namespace

void Account::sign_in_run() {
    const HttpSendFn send = [this](const HttpRequest &r) {
        HttpRequest q = r;
        q.cancel = &stop_;
        std::lock_guard<std::mutex> net(net_mu_);
        return send_(q);
    };
    std::string err;
    OAuthClient client;
    if (!fetch_tv_client(send, client, err)) return end_sign_in(err);
    DeviceCode dc;
    if (request_device_code(send, client, random_device_id(), dc, err) != OAuthResult::Ok) return end_sign_in(err);
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stop_) return;
        code_ = dc.user_code;
        url_ = dc.url;
    }
    changed();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(dc.expires_in);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait_for(lk, std::chrono::milliseconds(std::max<int64_t>(dc.interval * 1000, 20)), [this] { return stop_.load(); });
        }
        if (stop_) return;
        OAuthToken tok;
        switch (poll_device_token(send, client, dc.device_code, tok, err)) {
        case PollResult::Pending:
        case PollResult::Network:
            continue;
        case PollResult::SlowDown:
            dc.interval += 5;
            continue;
        case PollResult::Refused:
            return end_sign_in(err);
        case PollResult::Ok:
            break;
        }
        AccountInfo who;
        std::string e;
        const OAuthResult r = fetch_account(send, tok.access, who, e);
        if (r == OAuthResult::Refused) return end_sign_in(e);
        if (r == OAuthResult::Network) log_warn("account: name not fetched: " + e);
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (stop_) return;
            adopt_locked(tok.refresh, client, "own", who, tok);
            pending_ = false;
            code_.clear();
            url_.clear();
        }
        log_info("account: signed in (own)");
        changed();
        return;
    }
    end_sign_in("expired_token");
}

}  // namespace bbm
