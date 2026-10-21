#pragma once
#include "decoder.hpp"
#include "ringbuf.hpp"
#include "spsc.hpp"

// audio thread is in `audio.cpp`
// video thread is in `video.cpp`
// deocode threads are in `decode.cpp`

// decode threads include:
// 1 main decode thread -- the packet producer
// 2 sub-decode threads (audio and video) -- the frame producers

// "master clock", count manually by audio thread
extern std::atomic_int64_t audio_current_us;

// Handle seek is like this:
// video thread set seek_time_us + video_idle = true then seek_req, order matter
// then it wait for seek_req = false
//
// So when seek_req is true
// + audio will flush internal queue then also set idle and wait
// + sub-decode thread will quit
//
// This is STOP THE WORLD.
//
// Our main decoder thread will handle the seek.
// `seek_req` is back to false when done.
//
// This process usually does not cause stuttering.
// The synchronization part after this does.

// Synchronization: read `video.cpp` at `Video::render`

// set by audio thread
extern std::atomic_bool audio_idle;

// set by video thread
extern std::atomic_bool video_idle;

// set by video thread
extern std::atomic_bool paused;

// set by main decode thread
extern std::atomic_bool seek_available;

// set by video thread
extern std::atomic_bool seek_req;

// set by video thread
extern std::atomic_int64_t seek_time_us;

void clear_state();

namespace Packet {
namespace Video {
static constexpr int SOFT_LIMIT = 10;
extern SPSC::Sender<Decoder::Packet> sender;
extern SPSC::Receiver<Decoder::Packet> receiver;
}  // namespace Video
namespace Audio {
static constexpr int SOFT_LIMIT = 10;
extern SPSC::Sender<Decoder::Packet> sender;
extern SPSC::Receiver<Decoder::Packet> receiver;
}  // namespace Audio
void init_channels();
}  // namespace Packet

namespace Frame {
namespace Video {
static constexpr int CONCURRENT_LIMIT = 10;
extern RingBuf::Sender<Decoder::Frame> sender;
extern RingBuf::Receiver<Decoder::Frame> receiver;
}  // namespace Video
namespace Audio {
static constexpr int CONCURRENT_LIMIT = 10;
extern RingBuf::Sender<Decoder::Frame> sender;
extern RingBuf::Receiver<Decoder::Frame> receiver;
}  // namespace Audio
void init_channels();
}  // namespace Frame
