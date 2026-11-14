// Player: construction and the commands (any thread; each takes the lock, sets what the player
// thread is to do, and returns).
#include "player/player.h"

namespace bbm {

Player::Player(SourceFactory sources, SinkFactory sinks)
    : sink_factory_(std::move(sinks)),
      feed_(m_, cv_, std::move(sources), [this] { return successor(); }),
      thread_([this] { run(); }) {}

Player::~Player() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
        feed_.flush(false);   // cancels a read in flight
    }
    cv_.notify_all();
    thread_.join();
}

std::optional<Track> Player::successor() const {
    const int i = queue_.next_index();
    if (i < 0) return std::nullopt;
    return queue_.tracks()[size_t(i)];
}

PlayerStatus Player::snapshot() const {
    PlayerStatus s = st_;
    s.index = queue_.index();
    return s;
}

PlayerStatus Player::status() const {
    std::lock_guard<std::mutex> lk(m_);
    return snapshot();
}

std::vector<Track> Player::queue() const {
    std::lock_guard<std::mutex> lk(m_);
    return queue_.tracks();
}

void Player::set_listener(std::function<void(const PlayerEvent &)> fn) {
    std::lock_guard<std::mutex> lk(m_);
    listener_ = std::move(fn);
}

int64_t Player::buffered_ms_for_test() const {
    std::lock_guard<std::mutex> lk(m_);
    return feed_.buffered_ms();
}

int Player::reads_for_test() const {
    std::lock_guard<std::mutex> lk(m_);
    return feed_.reads();
}

// ---- commands -------------------------------------------------------------------------------

void Player::queue_changed() {
    queue_dirty_ = true;
    const int i = queue_.next_index();
    feed_.sync_next(i >= 0 ? &queue_.tracks()[size_t(i)] : nullptr);   // a prepared next may be stale
    cv_.notify_all();
}

// Start the track at queue index `index` from ms. The flush answers a slow read at once; the
// player thread then restarts the feed (so no chunk of the new start is seen before its request).
void Player::load_index(int index, int64_t ms) {
    queue_.jump(index);
    req_.load = true;
    req_.seek = false;
    req_.stop = false;
    req_.ms = ms;
    want_pause_ = false;
    feed_.flush(true);
    cv_.notify_all();
}

void Player::play(std::vector<Track> tracks, int index, int64_t position_ms) {
    std::lock_guard<std::mutex> lk(m_);
    if (tracks.empty()) {
        req_ = Req{};
        req_.stop = true;
        want_pause_ = false;
        feed_.flush(false);
        queue_.set({}, -1);
        queue_changed();
        return;
    }
    queue_.set(std::move(tracks), index);
    queue_dirty_ = true;
    load_index(queue_.index(), position_ms);
}

void Player::pause() {
    std::lock_guard<std::mutex> lk(m_);
    if (st_.state != PlayState::Stopped || req_.load) want_pause_ = true;
    cv_.notify_all();
}

void Player::resume() {
    std::lock_guard<std::mutex> lk(m_);
    want_pause_ = false;
    cv_.notify_all();
}

void Player::toggle() {
    std::lock_guard<std::mutex> lk(m_);
    if (want_pause_) want_pause_ = false;
    else if (st_.state != PlayState::Stopped || req_.load) want_pause_ = true;
    cv_.notify_all();
}

void Player::stop() {
    std::lock_guard<std::mutex> lk(m_);
    req_ = Req{};
    req_.stop = true;
    want_pause_ = false;
    feed_.flush(false);
    cv_.notify_all();
}

void Player::seek(int64_t ms) {
    std::lock_guard<std::mutex> lk(m_);
    if (ms < 0) ms = 0;
    if (req_.load) { req_.ms = ms; return; }   // the load has not started yet: start there
    if (st_.state == PlayState::Stopped || req_.stop) return;
    req_.seek = true;
    req_.ms = ms;
    feed_.flush(true);
    cv_.notify_all();
}

void Player::jump(int index) {
    std::lock_guard<std::mutex> lk(m_);
    if (index >= 0 && index < queue_.size()) load_index(index, 0);
}

void Player::next() {
    std::lock_guard<std::mutex> lk(m_);
    const int i = queue_.next_index();
    if (i >= 0) {
        load_index(i, 0);
    } else if (st_.state != PlayState::Stopped) {   // the end of the queue
        req_ = Req{};
        req_.stop = true;
        feed_.flush(false);
        cv_.notify_all();
    }
}

void Player::previous() {
    std::lock_guard<std::mutex> lk(m_);
    if (queue_.index() < 0) return;
    const int i = queue_.prev_index();
    if (i < 0 || (st_.position_ms > 3000 && !req_.load)) load_index(queue_.index(), 0);   // restart this one
    else load_index(i, 0);
}

void Player::enqueue(std::vector<Track> tracks, bool play_next) {
    std::lock_guard<std::mutex> lk(m_);
    if (tracks.empty()) return;
    queue_.add(std::move(tracks), play_next);
    queue_changed();
}

void Player::remove(int index) {
    std::lock_guard<std::mutex> lk(m_);
    if (index < 0 || index >= queue_.size()) return;
    const bool was_current = index == queue_.index();
    queue_.remove(index);
    if (was_current && (st_.state != PlayState::Stopped || req_.load)) {
        if (queue_.index() >= 0) {
            load_index(queue_.index(), 0);   // plays on with the track that followed it
        } else {
            req_ = Req{};
            req_.stop = true;
            feed_.flush(false);
        }
    }
    queue_changed();
}

void Player::move(int from, int to) {
    std::lock_guard<std::mutex> lk(m_);
    if (queue_.move(from, to)) queue_changed();
}

}  // namespace bbm
