#pragma once
#include "decoder.hpp"
#include "ringbuf.hpp"
#include "spsc.hpp"

namespace Packet {
namespace Video {
extern SPSC::Sender<Decoder::Packet> sender;
extern SPSC::Receiver<Decoder::Packet> receiver;
}  // namespace Video
namespace Audio {
extern SPSC::Sender<Decoder::Packet> sender;
extern SPSC::Receiver<Decoder::Packet> receiver;
}  // namespace Audio
void init_channels();
}  // namespace Packet

namespace Frame {
namespace Video {
extern RingBuf::Sender<Decoder::Frame> sender;
extern RingBuf::Receiver<Decoder::Frame> receiver;
}  // namespace Video
namespace Audio {
extern RingBuf::Sender<Decoder::Frame> sender;
extern RingBuf::Receiver<Decoder::Frame> receiver;
}  // namespace Audio
void init_channels();
}  // namespace Frame
