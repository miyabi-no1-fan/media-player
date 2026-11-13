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
#include <cstddef>
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

    // signal decoder to handle seek req
    std::atomic_bool decoder_seek_req = false;

    // signal audio to handle seek req
    std::atomic_bool audio_seek_req = false;

    // after a seek req, video decode usually slower than audio
    // which results in audio play then video just freeze
    // until video decode finally catch up.
    // our A/V sync logic doesn't help much here.
    // the blocking recv will wait until the first frame produce
    // is why it's freezing,
    // only after the very first frame received would A/V sync help
    // -- but decoder and channels has warmed up by then
    // so it's not necessary slow anymore.
    // So this flag is to fix the issue.
    std::atomic_bool audio_should_wait = false;

    // hold an AVERROR code if < 0
    std::atomic_int sub_decode_video_status = 0;
    std::atomic_int sub_decode_audio_status = 0;

    MPMCQueue<UniqueAVPacket> video_packet_queue;
    MPMCQueue<UniqueAVPacket> audio_packet_queue;
    MPMCQueue<UniqueAVFrame> video_frame_queue{ VIDEO_FRAME_QUEUE_BOUND };
    MPMCQueue<UniqueAVFrame> audio_frame_queue{ AUDIO_FRAME_QUEUE_BOUND };

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
    ~VideoState() { state->video_frame_queue.close(); }
    void init() { state = global_state.load(); }
    std::int64_t current_us() const { return state->current_us.load(); }
    bool is_paused() const { return state->paused.load(); }
    void pause() { state->paused.store(true); }
    void play() { state->paused.store(false); }
    bool is_seek_available() { return true; }
    // require a matching `seeking_release`
    void seeking_lock(std::int64_t target_us) {
        assert(is_seek_available());

        auto old_state = std::move(state);

        state = std::make_shared<State>();
        state->current_us.store(target_us);
        state->audio_should_wait.store(true);
        global_state.store(state);

        old_state->decoder_seek_req.store(true);
        old_state->audio_seek_req.store(true);

        old_state->paused.store(false);
        old_state->audio_should_wait.store(false);
        old_state->audio_should_wait.notify_one();

        old_state->video_frame_queue.close();
        old_state->audio_frame_queue.close();
    }
    // return true if you haven't call `seeking_release` after a `seeking_lock`
    bool is_seeking() { return state->audio_should_wait.load(); }
    void seeking_release() {
        state->audio_should_wait.store(false);
        state->audio_should_wait.notify_one();
    }
    std::optional<UniqueAVFrame> recv() { return state->video_frame_queue.pop(); }
    void close_queue() { state->video_frame_queue.close(); }
    bool has_all_queue_ended() {
        return state->video_frame_queue.empty() && state->video_frame_queue.is_closed() &&
               state->audio_frame_queue.empty() && state->audio_frame_queue.is_closed() &&
               state->video_packet_queue.empty() && state->video_packet_queue.is_closed() &&
               state->audio_packet_queue.empty() && state->audio_packet_queue.is_closed();
    }
};

class AudioState {
    std::shared_ptr<State> state;

   public:
    AudioState() = default;
    ~AudioState() { close_queue(); }
    void init() { state = global_state.load(); }
    bool is_paused() const { return state->paused.load(); }
    void next_current_us(std::int64_t step) { state->current_us.fetch_add(step); }
    bool is_seek() const { return state->audio_seek_req.load(); }
    void handle_seek() {
        assert(is_seek());
        state = global_state.load();
    }
    void wait_video() { state->audio_should_wait.wait(true); }
    std::optional<UniqueAVFrame> recv() { return state->audio_frame_queue.pop(); }
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
            state.wait_video();
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
    ~DecoderState() { close_connection(); }
    std::optional<UniqueAVPacket> send_video_packet(UniqueAVPacket packet) { return state->video_packet_queue.push(std::move(packet)); }
    std::optional<UniqueAVPacket> send_audio_packet(UniqueAVPacket packet) { return state->audio_packet_queue.push(std::move(packet)); }
    void close_connection() {
        state->video_packet_queue.close();
        state->audio_packet_queue.close();
    }
    std::size_t video_packet_queue_len() const { return state->video_packet_queue.size(); }
    std::size_t audio_packet_queue_len() const { return state->audio_packet_queue.size(); }
    std::int64_t current_us() { return state->current_us.load(); }
    bool is_seek() const { return state->decoder_seek_req.load(); }
    // return the target_us
    void handle_seek() {
        assert(is_seek());
        close_connection();
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
        state->video_packet_queue.close();
        state->video_frame_queue.close();
    }
    std::int64_t current_us() { return state->current_us.load(); }
    std::optional<UniqueAVFrame> send(UniqueAVFrame frame) { return state->video_frame_queue.push(std::move(frame)); }
    std::optional<UniqueAVPacket> recv() { return state->video_packet_queue.pop(); }
    void error(int e) { state->sub_decode_video_status.store(e); }
};

class SubDecoderAudioState {
    std::shared_ptr<State> state;

   public:
    SubDecoderAudioState() = default;
    void init() { state = global_state.load(); }
    ~SubDecoderAudioState() {
        state->audio_packet_queue.close();
        state->audio_frame_queue.close();
    }
    std::int64_t current_us() { return state->current_us.load(); }
    std::optional<UniqueAVFrame> send(UniqueAVFrame frame) { return state->audio_frame_queue.push(std::move(frame)); }
    std::optional<UniqueAVPacket> recv() { return state->audio_packet_queue.pop(); }
    void error(int e) { state->sub_decode_audio_status.store(e); }
};
