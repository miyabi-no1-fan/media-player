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
#include "state.hpp"

#include <atomic>
#include <memory>

#include "decoder.hpp"
#include "ringbuf.hpp"
#include "spsc.hpp"

std::atomic<std::shared_ptr<std::atomic_int64_t>> audio_current_us = nullptr;
std::atomic_bool paused = false;
std::atomic_bool seek_available = false;
std::atomic_bool decoder_seek_req = false;
std::atomic_bool audio_seek_req = false;
std::atomic_int64_t seek_time_us = 0;
std::atomic_bool seek_req_ok = false;

std::int64_t global_current_us() {
    if (auto current_us = audio_current_us.load()) return current_us->load();
    else return seek_time_us.load();
}

void video_call_this_to_do_a_seek(Video&, std::int64_t target_us) {
    paused.store(false);
    seek_time_us.store(target_us);
    audio_current_us.store(nullptr);
    decoder_seek_req.store(true);
    audio_seek_req.store(true);
    audio_seek_req.wait(true);
    seek_req_ok.wait(true);
}

bool should_audio_handle_seek() { return audio_seek_req.load(); }

void audio_call_this_to_handle_seek_but_you_have_to_clear_the_queue_urself_first(Audio& self) {
    audio_current_us.store(std::make_shared<std::atomic_int64_t>(0));
    self.current_us = audio_current_us;
    decoder_seek_req.wait(true);
    self.receiver = Frame::Audio::receiver;
    self.current_us->store(seek_time_us.load());
    audio_seek_req.store(false);
    audio_seek_req.notify_all();
    seek_req_ok.store(false);
    seek_req_ok.wait(false);
}

std::optional<std::int64_t> should_decoder_handle_seek() {
    if (decoder_seek_req.load()) return seek_time_us.load();
    return std::nullopt;
}

void decoder_have_handled_seek_flush_sth_by_itself_now_do_some_signaling_stuff() {
    decoder_seek_req.store(false);
    decoder_seek_req.notify_all();
}

void decoder_set_seek_availability(bool flag) { seek_available.store(flag); }

void video_tell_audio_seek_is_ok() {
    seek_req_ok.store(true);
    seek_req_ok.notify_all();
}

bool can_video_ask_for_more_seek() {
    return seek_req_ok.load() && seek_available.load();
}

void new_state() {
    audio_current_us.store(nullptr);
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
std::shared_ptr<SPSC::Sender<UniqueAVPacket>> sender = nullptr;
std::shared_ptr<SPSC::Receiver<UniqueAVPacket>> receiver = nullptr;
}  // namespace Video
namespace Audio {
std::shared_ptr<SPSC::Sender<UniqueAVPacket>> sender = nullptr;
std::shared_ptr<SPSC::Receiver<UniqueAVPacket>> receiver = nullptr;
}  // namespace Audio
void new_channels() {
    using namespace SPSC;
    using T = UniqueAVPacket;
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
std::shared_ptr<RingBuf::Sender<UniqueAVFrame>> sender;
std::shared_ptr<RingBuf::Receiver<UniqueAVFrame>> receiver;
}  // namespace Video
namespace Audio {
std::shared_ptr<RingBuf::Sender<UniqueAVFrame>> sender;
std::shared_ptr<RingBuf::Receiver<UniqueAVFrame>> receiver;
}  // namespace Audio
void new_channels() {
    using namespace RingBuf;
    using T = UniqueAVFrame;
    {
        auto [s, r] = bounded_channel<T>(Video::CONCURRENT_LIMIT);
        Frame::Video::sender = std::make_shared<Sender<T>>(std::move(s));
        Frame::Video::receiver = std::make_shared<Receiver<UniqueAVFrame>>(std::move(r));
    }
    {
        auto [s, r] = RingBuf::bounded_channel<T>(Audio::CONCURRENT_LIMIT);
        Frame::Audio::sender = std::make_shared<Sender<T>>(std::move(s));
        Frame::Audio::receiver = std::make_shared<Receiver<T>>(std::move(r));
    }
}
}  // namespace Frame
