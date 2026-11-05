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
#include <memory>
#include <optional>

#include "audio.hpp"
#include "decoder.hpp"
#include "ringbuf.hpp"
#include "spsc.hpp"
#include "video.hpp"

// audio thread is in `audio.cpp`
// video thread is in `video.cpp`
// deocode threads are in `decode.cpp`

// decode threads include:
// 1 main decode thread -- the packet producer
// 2 sub-decode threads (audio and video) -- the frame producers

// "master clock", create and manually counted by audio thread.
extern std::atomic<std::shared_ptr<std::atomic_int64_t>> audio_current_us;
std::int64_t global_current_us();

void video_call_this_to_do_a_seek(Video& you, std::int64_t where_you_wanna_seek);

bool should_audio_handle_seek();
void audio_call_this_to_handle_seek_but_you_have_to_clear_the_queue_urself_first(Audio& you);

std::optional<std::int64_t> should_decoder_handle_seek();
void decoder_have_handled_seek_flush_sth_by_itself_now_do_some_signaling_stuff();
void decoder_set_seek_availability(bool is_seek_available);

// `seek_req_ok` is set to true by video
// and set to false by audio.
// After decoder and audio finish their seek req
// audio has to wait until video frames ready to play.
// Video decoder can be slow and might miss a few frames
// to catch up.
void video_tell_audio_seek_is_ok();

bool can_video_ask_for_more_seek();

// set by video thread
extern std::atomic_bool paused;

void new_state();

namespace Packet {
namespace Video {
static constexpr int SOFT_LIMIT = 10;
extern std::shared_ptr<SPSC::Sender<UniqueAVPacket>> sender;
extern std::shared_ptr<SPSC::Receiver<UniqueAVPacket>> receiver;
}  // namespace Video
namespace Audio {
static constexpr int SOFT_LIMIT = 10;
extern std::shared_ptr<SPSC::Sender<UniqueAVPacket>> sender;
extern std::shared_ptr<SPSC::Receiver<UniqueAVPacket>> receiver;
}  // namespace Audio
void new_channels();
}  // namespace Packet

namespace Frame {
namespace Video {
static constexpr int CONCURRENT_LIMIT = 10;
extern std::shared_ptr<RingBuf::Sender<UniqueAVFrame>> sender;
extern std::shared_ptr<RingBuf::Receiver<UniqueAVFrame>> receiver;
}  // namespace Video
namespace Audio {
static constexpr int CONCURRENT_LIMIT = 10;
extern std::shared_ptr<RingBuf::Sender<UniqueAVFrame>> sender;
extern std::shared_ptr<RingBuf::Receiver<UniqueAVFrame>> receiver;
}  // namespace Audio
void new_channels();
}  // namespace Frame
