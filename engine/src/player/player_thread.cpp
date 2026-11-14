// Player: the player thread. It holds m_ except around decoding, sink calls and the listener.
#include <algorithm>

#include "player/player.h"
#include "util/log.h"

namespace bbm {

bool Player::pending() const {
    return interrupted() || queue_dirty_ ||
           (st_.state != PlayState::Stopped && want_pause_ != (st_.state == PlayState::Paused));
}

void Player::emit(std::unique_lock<std::mutex> &lk, PlayerEvent::Kind k) {
    PlayerEvent ev{k, snapshot()};
    auto fn = listener_;
    if (!fn) return;
    lk.unlock();
    fn(ev);
    lk.lock();
}

void Player::set_state(std::unique_lock<std::mutex> &lk, PlayState s) {
    if (st_.state == s) return;
    st_.state = s;
    emit(lk, PlayerEvent::State);
}

void Player::queue_low_check(std::unique_lock<std::mutex> &lk) {
    if (queue_.index() >= 0 && queue_.remaining() <= 2) emit(lk, PlayerEvent::QueueLow);
}

bool Player::ensure_sink(int channels, std::string &err) {
    if (!sink_) {
        sink_ = sink_factory_();
        if (!sink_) { err = "no audio sink"; return false; }
    }
    if (sink_open_ && sink_ch_ == channels) return true;
    if (sink_open_) { sink_->close(); sink_open_ = false; }
    if (!sink_->open(48000, channels, err)) return false;
    sink_open_ = true;
    sink_ch_ = channels;
    return true;
}

void Player::run() {
    std::unique_lock<std::mutex> lk(m_);
    while (!quit_) {
        if (apply_requests(lk)) continue;
        if (st_.state == PlayState::Stopped || st_.state == PlayState::Paused) {
            cv_.wait(lk, [&] { return pending(); });
            continue;
        }
        step(lk);
    }
    if (sink_open_) {
        lk.unlock();
        sink_->close();
        sink_open_ = false;
    }
}

bool Player::apply_requests(std::unique_lock<std::mutex> &lk) {
    if (queue_dirty_) {
        queue_dirty_ = false;
        emit(lk, PlayerEvent::Queue);
        queue_low_check(lk);
        return true;
    }
    if (req_.stop) { do_stop(lk); return true; }
    if (req_.load) { do_load(lk); return true; }
    if (req_.seek) { do_seek(lk); return true; }
    if (st_.state != PlayState::Stopped) {
        if (want_pause_ && st_.state != PlayState::Paused) { do_pause(lk); return true; }
        if (!want_pause_ && st_.state == PlayState::Paused) { do_resume(lk); return true; }
    }
    return false;
}

void Player::do_stop(std::unique_lock<std::mutex> &lk) {
    req_ = Req{};
    want_pause_ = false;
    feed_.flush(false);
    chain_.reset();
    awaiting_begin_ = announce_ = false;
    skip_serial_ = 0;
    if (sink_open_) {
        sink_open_ = false;
        lk.unlock();
        sink_->close();
        lk.lock();
    }
    st_.position_ms = 0;
    set_state(lk, PlayState::Stopped);
}

void Player::do_load(std::unique_lock<std::mutex> &lk) {
    const Track *t = queue_.current();
    if (!t) { do_stop(lk); return; }
    const Track tr = *t;
    const int64_t ms = req_.ms;
    req_.load = req_.seek = false;
    chain_.reset();
    skip_serial_ = 0;
    failures_ = 0;
    feed_.start(tr, ms);
    awaiting_begin_ = loading_ = true;
    announce_ = false;
    last_sec_ = ms / 1000;
    st_.track_id = tr.id;
    st_.position_ms = ms;
    st_.duration_ms = int64_t(tr.duration_s) * 1000;
    st_.error.clear();
    if (st_.state != PlayState::Paused) set_state(lk, PlayState::Loading);
    emit(lk, PlayerEvent::Track);
    queue_low_check(lk);
}

void Player::do_seek(std::unique_lock<std::mutex> &lk) {
    const Track *t = queue_.current();
    const int64_t ms = req_.ms;
    req_.seek = false;
    if (!t || st_.state == PlayState::Stopped) return;
    if (st_.duration_ms > 0 && ms >= st_.duration_ms) {   // past the end: the next track
        const int i = queue_.next_index();
        if (i < 0) { do_stop(lk); return; }
        queue_.jump(i);
        req_.load = true;
        req_.ms = 0;
        return;
    }
    chain_.reset();
    skip_serial_ = 0;
    feed_.start(*t, ms);
    awaiting_begin_ = true;
    announce_ = false;
    last_sec_ = ms / 1000;
    st_.position_ms = ms;
    emit(lk, PlayerEvent::Position);
}

void Player::do_pause(std::unique_lock<std::mutex> &lk) {
    // Writes have stopped: we are between two of them.
    if (sink_open_) {
        sink_open_ = false;
        lk.unlock();
        sink_->close();
        lk.lock();
    }
    set_state(lk, PlayState::Paused);
}

void Player::do_resume(std::unique_lock<std::mutex> &lk) {
    if (sink_ch_ > 0 && !sink_open_) {
        std::string err;
        lk.unlock();
        const bool ok = ensure_sink(sink_ch_, err);
        lk.lock();
        if (!ok) {
            st_.error = "sink: " + err;
            emit(lk, PlayerEvent::Error);
            do_stop(lk);
            return;
        }
    }
    set_state(lk, loading_ ? PlayState::Loading : PlayState::Playing);
}

// A track failed (could not be opened, read or decoded): say so, then go on to the next one,
// or stop after three failures in a row or at the end of the queue.
void Player::report_error(std::unique_lock<std::mutex> &lk, const std::string &msg) {
    log_warn("player: " + msg);
    st_.error = msg;
    ++failures_;
    emit(lk, PlayerEvent::Error);
    if (failures_ >= 3 || queue_.next_index() < 0) do_stop(lk);
}

void Player::step(std::unique_lock<std::mutex> &lk) {
    std::string err;
    lk.unlock();
    const DecodeChain::Result r = chain_.next(pcm_, err);
    lk.lock();
    if (r == DecodeChain::Error) {
        skip_serial_ = cur_serial_;   // the rest of this track's chunks are ignored
        chain_.reset();
        report_error(lk, st_.track_id + ": " + err);
        return;
    }
    if (r == DecodeChain::Frames) {
        if (!pcm_.empty() && !interrupted()) write_pcm(lk);
        return;
    }
    if (pending()) return;
    FeedChunk c;
    if (!feed_.pop(c)) {
        cv_.wait(lk, [&] { return pending() || feed_.has_chunk(); });
        return;
    }
    on_chunk(lk, c);
}

void Player::on_chunk(std::unique_lock<std::mutex> &lk, FeedChunk &c) {
    std::string err;
    switch (c.kind) {
    case FeedChunk::Begin:
        begin_track(lk, c);
        break;
    case FeedChunk::Seg: {
        if (c.serial == skip_serial_) break;
        lk.unlock();
        const bool ok = chain_.feed(c.seg.bytes, err);
        lk.lock();
        if (!ok) {
            skip_serial_ = c.serial;
            chain_.reset();
            report_error(lk, st_.track_id + ": " + err);
        }
        break;
    }
    case FeedChunk::End:
        if (c.serial == skip_serial_) { skip_serial_ = 0; break; }
        if (queue_.next_index() < 0) do_stop(lk);   // the queue is over; else the next Begin follows
        break;
    case FeedChunk::Fail:
        if (c.serial == skip_serial_) { skip_serial_ = 0; break; }
        cur_serial_ = c.serial;
        feed_.set_playing(c.serial);
        if (!c.began && !awaiting_begin_ && queue_.next_index() >= 0) {   // the next track would not open
            queue_.jump(queue_.next_index());
            st_.track_id = c.track.id;
            st_.position_ms = 0;
        }
        awaiting_begin_ = false;
        report_error(lk, c.track.id + ": " + c.err);
        break;
    }
}

void Player::begin_track(std::unique_lock<std::mutex> &lk, const FeedChunk &c) {
    const bool crossing = !awaiting_begin_;   // gapless: not the track a load or a seek asked for
    awaiting_begin_ = false;
    cur_serial_ = c.serial;
    feed_.set_playing(c.serial);
    if (crossing) {
        if (queue_.next_index() >= 0) queue_.jump(queue_.next_index());
        st_.track_id = c.track.id;
        st_.position_ms = 0;
        announce_ = true;   // the Track event waits for the first sample
    }
    st_.duration_ms = c.duration_ms;
    first_write_ = true;
    std::string err;
    lk.unlock();
    bool ok = chain_.begin(c.init, c.first_segment, c.target_ms, err);
    if (ok) ok = ensure_sink(chain_.channels(), err);
    lk.lock();
    if (!ok) {
        skip_serial_ = c.serial;
        chain_.reset();
        report_error(lk, c.track.id + ": " + err);
    }
}

void Player::write_pcm(std::unique_lock<std::mutex> &lk) {
    const size_t frames = pcm_.size() / size_t(chain_.channels());
    std::string err;
    lk.unlock();
    const bool ok = sink_->write(pcm_.data(), frames, err);
    lk.lock();
    if (!ok) {
        st_.error = "sink: " + err;
        emit(lk, PlayerEvent::Error);
        do_stop(lk);
        return;
    }
    if (interrupted()) return;   // a load or stop came in: the position is about to be reset
    st_.position_ms = chain_.position_ms();
    if (first_write_) {
        first_write_ = false;
        failures_ = 0;
        if (loading_) {
            loading_ = false;
            set_state(lk, PlayState::Playing);
        }
        if (announce_) {
            announce_ = false;
            emit(lk, PlayerEvent::Track);
            queue_low_check(lk);
        }
    }
    const int64_t sec = st_.position_ms / 1000;
    if (sec != last_sec_) {
        last_sec_ = sec;
        emit(lk, PlayerEvent::Position);
    }
}

}  // namespace bbm
