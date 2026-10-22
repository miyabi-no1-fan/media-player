#include "state.hpp"

#include <atomic>
#include <memory>

#include "decoder.hpp"
#include "ringbuf.hpp"
#include "spsc.hpp"

std::shared_ptr<std::atomic_int64_t> audio_current_us = nullptr;
std::atomic_bool paused = false;
std::atomic_bool seek_available = false;
std::atomic_bool decoder_seek_req = false;
std::atomic_bool audio_seek_req = false;
std::atomic_int64_t seek_time_us = 0;
std::atomic_bool seek_req_ok = false;

void new_state() {
    audio_current_us = std::make_shared<std::atomic_int64_t>(0);
    paused.store(false);
    seek_available.store(false);
    decoder_seek_req.store(false);
    audio_seek_req.store(false);
    seek_time_us.store(0);
    seek_req_ok.store(true);
    Packet::new_channels();
    Frame::new_channels();
}

namespace Packet {
namespace Video {
std::shared_ptr<SPSC::Sender<Decoder::Packet>> sender = nullptr;
std::shared_ptr<SPSC::Receiver<Decoder::Packet>> receiver = nullptr;
}  // namespace Video
namespace Audio {
std::shared_ptr<SPSC::Sender<Decoder::Packet>> sender = nullptr;
std::shared_ptr<SPSC::Receiver<Decoder::Packet>> receiver = nullptr;
}  // namespace Audio
void new_channels() {
    using namespace SPSC;
    using T = Decoder::Packet;
    {
        auto [s, r] = unbounded_channel<T>();
        Packet::Video::sender = std::make_shared<Sender<T>>(std::move(s));
        Packet::Video::receiver = std::make_shared<Receiver<T>>(std::move(r));
    }
    {
        auto [s, r] = SPSC::unbounded_channel<T>();
        Packet::Audio::sender = std::make_shared<Sender<T>>(std::move(s));
        Packet::Audio::receiver = std::make_shared<Receiver<T>>(std::move(r));
    }
}
}  // namespace Packet

namespace Frame {
namespace Video {
std::shared_ptr<RingBuf::Sender<Decoder::Frame>> sender;
std::shared_ptr<RingBuf::Receiver<Decoder::Frame>> receiver;
}  // namespace Video
namespace Audio {
std::shared_ptr<RingBuf::Sender<Decoder::Frame>> sender;
std::shared_ptr<RingBuf::Receiver<Decoder::Frame>> receiver;
}  // namespace Audio
void new_channels() {
    using namespace RingBuf;
    using T = Decoder::Frame;
    {
        auto [s, r] = bounded_channel<T>(Video::CONCURRENT_LIMIT);
        Frame::Video::sender = std::make_shared<Sender<T>>(std::move(s));
        Frame::Video::receiver = std::make_shared<Receiver<Decoder::Frame>>(std::move(r));
    }
    {
        auto [s, r] = RingBuf::bounded_channel<T>(Audio::CONCURRENT_LIMIT);
        Frame::Audio::sender = std::make_shared<Sender<T>>(std::move(s));
        Frame::Audio::receiver = std::make_shared<Receiver<T>>(std::move(r));
    }
}
}  // namespace Frame
