/*
    media-player
    Copyright (C) 2026  miyabi-no1-fan

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>

#include "decoder.hpp"
#include "mpmc.hpp"

// audio thread is in `audio.cpp`
// video thread is in `video.cpp`
// deocode threads are in `decode.cpp`

// decode threads include:
// 1 main decode thread -- the packet producer
// 2 sub-decode threads (audio and video) -- the frame producers

// Packet queue use soft limit because the main decode thread is single threaded
// and it produces 1 packet at a time, that might be an audio packet or a video packet.
// If we use normal bound, the video queue may be full while audio is starving.
// So we have to use a soft limit, where main decode thread only wait when both
// audio and video is full.
constexpr std::size_t VIDEO_PACKET_QUEUE_SOFT_LIMIT = 10;
constexpr std::size_t AUDIO_PACKET_QUEUE_SOFT_LIMIT = 10;
constexpr std::size_t VIDEO_FRAME_QUEUE_BOUND = 30;
constexpr std::size_t AUDIO_FRAME_QUEUE_BOUND = 50;

class VideoState;
class AudioState;
class DecoderState;
class SubDecoderVideoState;
class SubDecoderAudioState;

class State {
   private:
    std::atomic_int64_t current_us = 0;
    std::atomic_bool paused = false;
    std::atomic_bool seek_req = false;

    // Video underrun will need audio to wait to catch up
    std::atomic_bool audio_should_wait = false;

    // hold an AVERROR code if < 0
    std::atomic_int sub_decode_video_status = 0;
    std::atomic_int sub_decode_audio_status = 0;

    // for audio and decoder, a failed send or recv could mean should quit or a seek_req
    MPMCQueue<UniqueAVPacket> video_packet_queue;
    MPMCQueue<UniqueAVPacket> audio_packet_queue;
    MPMCQueue<UniqueAVFrame> video_frame_queue{ VIDEO_FRAME_QUEUE_BOUND };
    MPMCQueue<UniqueAVFrame> audio_frame_queue{ AUDIO_FRAME_QUEUE_BOUND };

    // `*_packet_queue` use a `*_PACKET_QUEUE_SOFT_LIMIT` implementation
    // so this is the blocking waker for it
    std::atomic_bool packet_queue_waker;

   public:
    State() {}
    ~State() {
        video_packet_queue.close();
        audio_packet_queue.close();
        video_frame_queue.close();
        audio_frame_queue.close();
    }

    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;

    friend VideoState;
    friend AudioState;
    friend DecoderState;
    friend SubDecoderVideoState;
    friend SubDecoderAudioState;
};

// NO ONE IS ALLOWED TO USE THIS VARIABLE OUTSIDE OF `state.hpp`
extern std::atomic<std::shared_ptr<State>> global_state;
// ONLY CALL IN `main.cpp`
inline void new_state() { global_state = std::make_shared<State>(); }

class VideoState {
    std::shared_ptr<State> state;

   public:
    VideoState() = default;
    ~VideoState() {
        if (state) close_queue();
    }
    void init() { state = global_state.load(); }
    std::int64_t current_us() const { return state->current_us.load(); }
    bool is_paused() const { return state->paused.load(); }
    void pause() { state->paused.store(true); }
    void play() { state->paused.store(false); }
    bool is_seek_available() { return true; }
    // require a matching `seeking_release`
    void seek(std::int64_t target_us) {
        assert(is_seek_available());

        auto old_state = std::move(state);

        state = std::make_shared<State>();
        state->current_us.store(target_us);
        global_state.store(state);

        old_state->seek_req.store(true);

        // close all queue
        old_state->video_frame_queue.close();
        old_state->audio_frame_queue.close();
        old_state->video_packet_queue.close();
        old_state->audio_packet_queue.close();

        // wake up decoder
        old_state->packet_queue_waker.store(true);
        old_state->packet_queue_waker.notify_one();

        // wake up audio
        old_state->paused.store(false);
        old_state->audio_should_wait.store(false);
    }
    bool is_audio_waiting() { return state->audio_should_wait.load(); }
    void audio_should_wait(bool flag) { state->audio_should_wait.store(flag); }
    std::optional<UniqueAVFrame> recv() { return state->video_frame_queue.pop(); }
    template <typename D>
    std::expected<UniqueAVFrame, std::cv_status>
    recv_deadline(D&& deadline) { return state->video_frame_queue.pop_deadline(deadline); }
    void close_queue() { state->video_frame_queue.close(); }
    bool has_all_queue_ended() {
        return state->video_frame_queue.empty() && state->video_frame_queue.is_closed() &&
               state->audio_frame_queue.empty() && state->audio_frame_queue.is_closed() &&
               state->video_packet_queue.empty() && state->video_packet_queue.is_closed() &&
               state->audio_packet_queue.empty() && state->audio_packet_queue.is_closed();
    }
    void wait_until_all_queue_is_closed() {
        state->video_frame_queue.wait_for_close();
        state->audio_frame_queue.wait_for_close();
        state->video_packet_queue.wait_for_close();
        state->audio_packet_queue.wait_for_close();
    }
};

inline void handle_no_window_play() {
    VideoState state;
    state.init();
    state.close_queue();
    state.wait_until_all_queue_is_closed();
    while (!state.has_all_queue_ended()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

class AudioState {
    std::shared_ptr<State> state;

   public:
    AudioState() = default;
    ~AudioState() {
        if (state) close_queue();
    }
    void init() { state = global_state.load(); }
    bool is_paused() const { return state->paused.load() || state->audio_should_wait.load(); }
    std::int64_t current_us() const { return state->current_us.load(); }
    void next_current_us(std::int64_t step) { state->current_us.fetch_add(step); }
    bool is_seek() const { return state->seek_req.load(); }
    void handle_seek() {
        assert(is_seek());
        state = global_state.load();
    }
    std::optional<UniqueAVFrame> recv() { return state->audio_frame_queue.pop(); }
    template <typename D>
    std::expected<UniqueAVFrame, std::cv_status>
    recv_deadline(D&& deadline) { return state->audio_frame_queue.pop_deadline(deadline); }
    void close_queue() { state->audio_frame_queue.close(); }
};

inline std::jthread handle_no_audio_play() {
    return std::jthread([](std::stop_token should_stop) {
        using namespace std::chrono;
        microseconds step(1000);
        AudioState state;
        state.init();
        state.close_queue();
        time_point<steady_clock> next = steady_clock::now() + step;
        while (!should_stop.stop_requested()) {
            if (!state.is_paused()) {
                state.next_current_us(step.count());
                if (state.is_seek()) state.handle_seek();
            }

            std::this_thread::sleep_until(next);
            next += step;

            // resync if overshoot
            auto now = steady_clock::now();
            if (now > next) next = now + step;
        }
    });
}

class DecoderState {
    std::shared_ptr<State> state;

   public:
    DecoderState() = default;
    void init() { state = global_state.load(); }
    ~DecoderState() {
        if (state) close_connection();
    }
    std::optional<UniqueAVPacket> send_video_packet(UniqueAVPacket packet) { return state->video_packet_queue.push(std::move(packet)); }
    std::optional<UniqueAVPacket> send_audio_packet(UniqueAVPacket packet) { return state->audio_packet_queue.push(std::move(packet)); }
    void close_connection() {
        state->video_packet_queue.close();
        state->audio_packet_queue.close();
    }
    bool waiting_for_packet_queue() const {
        state->packet_queue_waker.store(false);
        if ((!state->video_packet_queue.is_closed() && state->video_packet_queue.size() < VIDEO_PACKET_QUEUE_SOFT_LIMIT) ||
            (!state->audio_packet_queue.is_closed() && state->audio_packet_queue.size() < AUDIO_PACKET_QUEUE_SOFT_LIMIT))
            return false;
        if (!state->video_packet_queue.is_closed() || !state->audio_packet_queue.is_closed()) {
            state->packet_queue_waker.wait(false);
        }
        return true;
    }
    std::int64_t current_us() { return state->current_us.load(); }
    bool is_seek() const { return state->seek_req.load(); }
    void handle_seek() {
        assert(is_seek());
        state = global_state.load();
    }
    int sub_decode_video_status() { return state->sub_decode_video_status.load(); }
    int sub_decode_audio_status() { return state->sub_decode_audio_status.load(); }
};

class SubDecoderVideoState {
    std::shared_ptr<State> state;

   public:
    SubDecoderVideoState() = default;
    void init() { state = global_state.load(); }
    ~SubDecoderVideoState() {
        if (state) close_connection();
    }
    std::int64_t current_us() { return state->current_us.load(); }
    std::optional<UniqueAVFrame> send(UniqueAVFrame frame) { return state->video_frame_queue.push(std::move(frame)); }
    std::optional<UniqueAVPacket> recv() {
        auto res = state->video_packet_queue.pop();
        state->packet_queue_waker.store(true);
        state->packet_queue_waker.notify_one();
        return res;
    }
    void error(int e) { state->sub_decode_video_status.store(e); }
    void close_connection() {
        state->video_packet_queue.close();
        state->video_frame_queue.close();
        state->packet_queue_waker.store(true);
        state->packet_queue_waker.notify_one();
    }
    bool is_seek() const { return state->seek_req.load(); }
    void handle_seek() {
        assert(is_seek());
        state = global_state.load();
    }
};

class SubDecoderAudioState {
    std::shared_ptr<State> state;

   public:
    SubDecoderAudioState() = default;
    void init() { state = global_state.load(); }
    ~SubDecoderAudioState() {
        if (state) close_connection();
    }
    std::int64_t current_us() { return state->current_us.load(); }
    std::optional<UniqueAVFrame> send(UniqueAVFrame frame) { return state->audio_frame_queue.push(std::move(frame)); }
    std::optional<UniqueAVPacket> recv() {
        auto res = state->audio_packet_queue.pop();
        state->packet_queue_waker.store(true);
        state->packet_queue_waker.notify_one();
        return res;
    }
    void error(int e) { state->sub_decode_audio_status.store(e); }
    void close_connection() {
        state->audio_packet_queue.close();
        state->audio_frame_queue.close();
        state->packet_queue_waker.store(true);
        state->packet_queue_waker.notify_one();
    }
    bool is_seek() const { return state->seek_req.load(); }
    void handle_seek() {
        assert(is_seek());
        state = global_state.load();
    }
};
