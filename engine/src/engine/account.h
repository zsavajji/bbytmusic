// The signed-in YouTube account: a refresh token (kept in the store) and the access token made
// from it (kept in memory). Thread-safe; no Engine here.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "engine/oauth.h"

namespace bbm {

class Account {
public:
    struct Info {
        bool in = false;
        std::string name, photo, source;
        bool pending = false;           // a sign-in is in flight
        std::string code, url, error;   // what to enter and where ("" until the service answered); why the last sign-in failed
    };
    explicit Account(HttpSendFn send) : send_(std::move(send)) {}
    ~Account();

    void load(const std::string &stored);   // what stored() gave; "" or garbage: signed out. No callback, no network.
    std::string stored() const;             // "" when signed out, else a JSON object with the refresh token and the client
    Info info() const;

    // A refresh token from elsewhere. An empty client (id and secret "") is looked up on the TV page.
    // One refresh proves the token and the pair, then the account's name is asked; only on Ok does
    // anything change. A name that cannot be fetched (Network) leaves it empty.
    OAuthResult import(const std::string &refresh, OAuthClient client, const std::string &source, std::string &err);
    // Stops a sign-in in flight; forgets, no network; no callback when already out and nothing pending.
    // Returns the refresh token to revoke when the account was our own ("" otherwise).
    std::string sign_out();
    void revoke(const std::string &token);   // best effort; a failure is a warning

    // The device-code sign-in: returns at once, replaces one in flight; the end comes by on_change
    // (signed in with source "own", or `error`).
    void sign_in();
    void cancel_sign_in();   // no-op when none is in flight

    // A valid access token, refreshed when under 60 s are left. Refused: Google no longer accepts
    // the grant, so the account is signed out. Network: nothing changes.
    OAuthResult bearer(std::string &out, std::string &err);
    // One request over the account's transport (one Http, not thread-safe: every use takes net_mu_).
    HttpResponse send(const HttpRequest &r) {
        std::lock_guard<std::mutex> lk(net_mu_);
        return send_(r);
    }
    void invalidate();   // forgets the access token: the next bearer() refreshes (after a 401)

    std::function<void()> on_change;   // set before use; after every change of info() or stored(), never under the mutex

private:
    void changed() {
        if (on_change) on_change();
    }
    std::string stored_locked() const;
    void adopt_locked(const std::string &refresh, const OAuthClient &client, const std::string &source, const AccountInfo &who,
                      const OAuthToken &tok);
    bool halt_locked();   // th_mu_ held
    bool halt();   // stops the sign-in thread; true when a sign-in was pending (now cleared, the caller fires on_change)
    void sign_in_run();
    void end_sign_in(const std::string &err);

    HttpSendFn send_;
    std::mutex net_mu_;               // every use of send_ (the Http behind it is not thread-safe); taken before mu_
    mutable std::mutex mu_;           // the fields only, never across a network call
    std::mutex th_mu_;                // th_ and stop_; taken before net_mu_
    std::thread th_;
    std::atomic<bool> stop_{false};
    std::condition_variable cv_;      // with mu_: the wait between polls
    bool pending_ = false;
    std::string code_, url_, error_;
    std::string refresh_, source_, name_, photo_;
    OAuthClient client_;
    std::string access_;      // memory only
    std::chrono::steady_clock::time_point expires_;
};

}  // namespace bbm
