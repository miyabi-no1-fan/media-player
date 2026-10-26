#pragma once
#include <atomic>
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
// if `seek_req_ok == true`, `audio_seek_req` = false.

// So a seek chain is
// `decoder_seek_req`, set true by video, audio wait for false.
// `audio_seek_req`, set true by video, video wait for false.
// `seek_req_ok`, set false by video, audio wait for true.

// atomic ordering are `seq_cst`

// set by video thread
extern std::atomic_bool paused;

// set by main decode thread
// main decode thread will not handle any
// `decoder_seek_req` when this is false
extern std::atomic_bool seek_available;

// `decoder_seek_req` is set to true by video and
// set to false when decoder finish its seek request.
// must set before `audio_seek_req`.
extern std::atomic_bool decoder_seek_req;

// `audio_seek_req` is set to true by video and
// set to false when audio finish its seek request.
// must set after `decoder_seek_req`.
extern std::atomic_bool audio_seek_req;

// set by video thread
// must set before `*_seek_req`.
extern std::atomic_int64_t seek_time_us;

// `seek_req_ok` is set to true by video
// and set to false by audio.
// After decoder and audio finish their seek req
// audio has to wait until video frames ready to play.
// Video decoder can be slow and might miss a few frames
// to catch up.
extern std::atomic_bool seek_req_ok;

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
