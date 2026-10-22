#pragma once
#include <memory>

#include "decoder.hpp"
#include "ringbuf.hpp"
#include "spsc.hpp"

// audio thread is in `audio.cpp`
// video thread is in `video.cpp`
// deocode threads are in `decode.cpp`

// decode threads include:
// 1 main decode thread -- the packet producer
// 2 sub-decode threads (audio and video) -- the frame producers

// "master clock", create and manually counted by audio thread.
extern std::shared_ptr<std::atomic_int64_t> audio_current_us;

// Video thread should always use the latest channels.
// Main decoder thread should always use the latest channels.
// Sub-decode threads should use the copied state at creation.
// Audio thread should use the copied state and only update
// for a `audio_seek_req`.

// if `audio_seek_req == false`, `audio_current_us` must not be nullptr.
// if `decoder_seek_req == false`, current shared channels must not be nullptr.
// if `audio_seek_req == false`, `decoder_seek_req` = true.

// atomic ordering are `seq_cst`

// NOTE: `deocder_seek_req` and `audio_seek_req` only
// call `notify_one` after being set to false
// because currently, only 1 thread call `wait`

// set by video thread
extern std::atomic_bool paused;

// set by main decode thread
// main decode thread will not handle any
// `decoder_seek_req` when this is false
extern std::atomic_bool seek_available;

// set true by video thread
// set false by main decode thread
extern std::atomic_bool decoder_seek_req;

// set true by video thread
// set false by audio thread
// required `decoder_seek_req` to be set
// before this.
extern std::atomic_bool audio_seek_req;

// set by video thread
// must set before `*_seek_req` with `seq_cst` ordering
extern std::atomic_int64_t seek_time_us;

void new_state();

namespace Packet {
namespace Video {
static constexpr int SOFT_LIMIT = 10;
extern std::shared_ptr<SPSC::Sender<Decoder::Packet>> sender;
extern std::shared_ptr<SPSC::Receiver<Decoder::Packet>> receiver;
}  // namespace Video
namespace Audio {
static constexpr int SOFT_LIMIT = 10;
extern std::shared_ptr<SPSC::Sender<Decoder::Packet>> sender;
extern std::shared_ptr<SPSC::Receiver<Decoder::Packet>> receiver;
}  // namespace Audio
void new_channels();
}  // namespace Packet

namespace Frame {
namespace Video {
static constexpr int CONCURRENT_LIMIT = 10;
extern std::shared_ptr<RingBuf::Sender<Decoder::Frame>> sender;
extern std::shared_ptr<RingBuf::Receiver<Decoder::Frame>> receiver;
}  // namespace Video
namespace Audio {
static constexpr int CONCURRENT_LIMIT = 10;
extern std::shared_ptr<RingBuf::Sender<Decoder::Frame>> sender;
extern std::shared_ptr<RingBuf::Receiver<Decoder::Frame>> receiver;
}  // namespace Audio
void new_channels();
}  // namespace Frame
