#include "decoder.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <expected>
#include <iostream>
#include <limits>
#include <optional>
#include <thread>
#include <utility>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "ringbuf.hpp"
#include "spsc.hpp"

static constexpr int VIDEO_PACKET_CHANNEL_SOFT_LIMNIT = 4;
static constexpr int AUDIO_PACKET_CHANNEL_SOFT_LIMNIT = 4;

// hold an AVERROR code if < 0
static std::atomic_int video_decoder_status = 0;
static std::atomic_int audio_decoder_status = 0;

std::thread Decoder::launch(RingBuf::Sender<Frame> video_sender,
                            RingBuf::Sender<Frame> audio_sender) {
    return std::thread([this, video_sender = std::move(video_sender), audio_sender = std::move(audio_sender)]() mutable {
        auto [video_packet_sender, video_packet_receiver] = SPSC::unbounded_channel<Packet>();
        auto [audio_packet_sender, audio_packet_receiver] = SPSC::unbounded_channel<Packet>();

        std::thread video_thrd, audio_thrd;
        if (video) video_thrd = launch_video_decoder(std::move(video_sender), std::move(video_packet_receiver));
        if (audio) audio_thrd = launch_audio_decoder(std::move(audio_sender), std::move(audio_packet_receiver));

        Packet p = av_packet_alloc();
        while ((video || audio) && av_read_frame(ictx, p.packet) >= 0) {
            if (video && p.packet->stream_index == video->stream_index)
                video_packet_sender.send(std::move(p));
            else if (audio && p.packet->stream_index == audio->stream_index)
                audio_packet_sender.send(std::move(p));
            p = av_packet_alloc();

            while (video || audio) {
                unsigned long v = std::numeric_limits<unsigned long>::max();
                if (video) v = video_packet_sender.len();
                unsigned long a = std::numeric_limits<unsigned long>::max();
                if (audio) a = audio_packet_sender.len();
                if (v <= VIDEO_PACKET_CHANNEL_SOFT_LIMNIT ||
                    a <= AUDIO_PACKET_CHANNEL_SOFT_LIMNIT) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                if (video_decoder_status.load(std::memory_order::relaxed) < 0)
                    video = std::nullopt;
                if (audio_decoder_status.load(std::memory_order::relaxed) < 0)
                    audio = std::nullopt;
            }
        }

        if (video) video_packet_sender.drop_connection();
        if (audio) audio_packet_sender.drop_connection();

        if (video_thrd.joinable()) video_thrd.join();
        if (audio_thrd.joinable()) audio_thrd.join();
    });
}

std::thread Decoder::launch_video_decoder(RingBuf::Sender<Frame> sender, SPSC::Receiver<Decoder::Packet> receiver) {
    assert(video);
    return std::thread([this, sender = std::move(sender), receiver = std::move(receiver)]() mutable {
        try {
            Frame f = av_frame_alloc();
            auto receive_franes = [&]() {
                while (avcodec_receive_frame(video->decoder, f.frame) >= 0) {
                    if (video->sws) {
                        Frame out = av_frame_alloc();
                        int res = sws_scale_frame(video->sws, out.frame, f.frame);
                        if (res < 0) throw res;
                        av_frame_unref(f.frame);
                        sender.send(std::move(out));
                    } else {
                        sender.send(std::move(f));
                        f.frame = av_frame_alloc();
                    }
                }
            };

            while (auto rec = receiver.receive()) {
                if (avcodec_send_packet(video->decoder, rec->packet) >= 0)
                    receive_franes();
            }

            int res = avcodec_send_packet(video->decoder, nullptr);
            if (res < 0) throw res;
            receive_franes();
        } catch (int err) {
            video_decoder_status.store(err, std::memory_order::relaxed);
            std::cerr << "Video decode error: " << err << "\n";
            return;
        }
    });
}

std::thread Decoder::launch_audio_decoder(RingBuf::Sender<Frame> sender, SPSC::Receiver<Decoder::Packet> receiver) {
    assert(audio);
    return std::thread([this, sender = std::move(sender), receiver = std::move(receiver)]() mutable {
        try {
            auto resample = [this](const AVFrame* input) -> std::expected<Frame, int> {
                Frame out = av_frame_alloc();
                int64_t sample_rate;
                av_opt_get_int(audio->swr, "out_sample_rate", 0, &sample_rate);
                out.frame->sample_rate = static_cast<int>(sample_rate);
                av_opt_get_sample_fmt(audio->swr, "out_sample_fmt", 0, (AVSampleFormat*)&out.frame->format);
                av_opt_get_chlayout(audio->swr, "out_chlayout", 0, &out.frame->ch_layout);
                int res = swr_convert_frame(audio->swr, out.frame, input);
                if (res < 0) return std::unexpected(res);
                assert(out.frame);
                return out;
            };

            Frame f = av_frame_alloc();
            auto receive_franes = [&]() {
                while (avcodec_receive_frame(audio->decoder, f.frame) >= 0) {
                    if (audio->swr) {
                        auto res = resample(f.frame);
                        if (!res) throw res.error();
                        av_frame_unref(f.frame);
                        sender.send(std::move(*res));
                    } else {
                        sender.send(std::move(f));
                        f = av_frame_alloc();
                    }
                }
            };

            while (auto rec = receiver.receive()) {
                if (avcodec_send_packet(audio->decoder, rec->packet) >= 0)
                    receive_franes();
            }

            int res = avcodec_send_packet(audio->decoder, nullptr);
            if (res < 0) throw res;
            receive_franes();

            if (audio->swr) {
                while (auto res = resample(nullptr)) {
                    if (res->frame->nb_samples == 0) break;
                    sender.send(std::move(*res));
                }
            }
        } catch (int err) {
            audio_decoder_status.store(err, std::memory_order::relaxed);
            std::cerr << "Audio decode error: " << err << "\n";
            return;
        }
    });
}
