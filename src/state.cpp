#include "state.hpp"

#include "decoder.hpp"
#include "ringbuf.hpp"
#include "spsc.hpp"

std::atomic_int64_t audio_current_us = 0;

std::atomic_bool audio_idle = false;
std::atomic_bool video_idle = false;

std::atomic_bool paused = false;

std::atomic_bool seek_available = true;
std::atomic_bool seek_req = false;
std::atomic_int64_t seek_time_us = 0;

void clear_state() {
    audio_current_us.store(0);
    audio_idle.store(false);
    video_idle.store(false);
    paused.store(false);
    seek_available.store(false);
    seek_req.store(false);
    seek_time_us.store(0);
}

namespace Packet {
namespace Video {
SPSC::Sender<Decoder::Packet> sender;
SPSC::Receiver<Decoder::Packet> receiver;
}  // namespace Video
namespace Audio {
SPSC::Sender<Decoder::Packet> sender;
SPSC::Receiver<Decoder::Packet> receiver;
}  // namespace Audio
void init_channels() {
    {
        auto [s, r] = SPSC::unbounded_channel<Decoder::Packet>();
        Packet::Video::sender = std::move(s);
        Packet::Video::receiver = std::move(r);
    }
    {
        auto [s, r] = SPSC::unbounded_channel<Decoder::Packet>();
        Packet::Audio::sender = std::move(s);
        Packet::Audio::receiver = std::move(r);
    }
}

}  // namespace Packet

namespace Frame {
namespace Video {
RingBuf::Sender<Decoder::Frame> sender;
RingBuf::Receiver<Decoder::Frame> receiver;
}  // namespace Video
namespace Audio {
RingBuf::Sender<Decoder::Frame> sender;
RingBuf::Receiver<Decoder::Frame> receiver;
}  // namespace Audio
void init_channels() {
    {
        auto [s, r] = RingBuf::bounded_channel<Decoder::Frame>(Video::CONCURRENT_LIMIT);
        Frame::Video::sender = std::move(s);
        Frame::Video::receiver = std::move(r);
    }
    {
        auto [s, r] = RingBuf::bounded_channel<Decoder::Frame>(Audio::CONCURRENT_LIMIT);
        Frame::Audio::sender = std::move(s);
        Frame::Audio::receiver = std::move(r);
    }
}

}  // namespace Frame
