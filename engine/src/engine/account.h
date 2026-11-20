// The signed-in YouTube account: a refresh token (kept in the store) and the access token made
// from it (kept in memory). Thread-safe; no Engine here.
#pragma once
#include <chrono>
#include <functional>
#include <mutex>
#include <string>

#include "engine/oauth.h"

namespace bbm {

class Account {
public:
    struct Info {
        bool in = false;
        std::string name, photo, source;
    };
    explicit Account(HttpSendFn send) : send_(std::move(send)) {}

    void load(const std::string &stored);   // what stored() gave; "" or garbage: signed out. No callback, no network.
    std::string stored() const;             // "" when signed out, else a JSON object with the refresh token and the client
    Info info() const;

    // A refresh token from elsewhere. An empty client (id and secret "") is looked up on the TV page.
    // One refresh proves the token and the pair, then the account's name is asked; only on Ok does
    // anything change. A name that cannot be fetched (Network) leaves it empty.
    OAuthResult import(const std::string &refresh, OAuthClient client, const std::string &source, std::string &err);
    void sign_out();   // forgets, no network; no callback when already out

    // A valid access token, refreshed when under 60 s are left. Refused: Google no longer accepts
    // the grant, so the account is signed out. Network: nothing changes.
    OAuthResult bearer(std::string &out, std::string &err);

    std::function<void()> on_change;   // set before use; after every change of info() or stored(), never under the mutex

private:
    void changed() {
        if (on_change) on_change();
    }
    std::string stored_locked() const;

    HttpSendFn send_;
    mutable std::mutex mu_;   // held across the network calls: one at a time
    std::string refresh_, source_, name_, photo_;
    OAuthClient client_;
    std::string access_;      // memory only
    std::chrono::steady_clock::time_point expires_;
};

}  // namespace bbm
