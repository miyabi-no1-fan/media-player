#include "decoder.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
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
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "state.hpp"

// hold an AVERROR code if < 0
static std::atomic_int video_decoder_status = 0;
static std::atomic_int audio_decoder_status = 0;

std::atomic_bool seek_available = true;
std::atomic_bool seek_req = false;
std::atomic_int64_t seek_time_us = 0;

extern std::atomic_bool audio_idle;
extern std::atomic_bool video_idle;

std::thread Decoder::launch() {
    return std::thread([this]() mutable {
        video_decoder_status.store(0);
        audio_decoder_status.store(0);
        seek_available.store(true);
        seek_req.store(false);

        std::thread video_thrd, audio_thrd;
        if (video) video_thrd = launch_video_decoder();
        if (audio) audio_thrd = launch_audio_decoder();

        Packet p = av_packet_alloc();
        while ((video || audio) && av_read_frame(ictx, p.packet) >= 0) {
            if (video && p.packet->stream_index == video->stream_index)
                ::Packet::Video::sender.send(std::move(p));
            else if (audio && p.packet->stream_index == audio->stream_index)
                ::Packet::Audio::sender.send(std::move(p));
            p = av_packet_alloc();

            while (video || audio) {
                if (seek_req.load()) {
                    std::int64_t ts = seek_time_us.load();
                    avformat_seek_file(ictx, -1, ts - AV_TIME_BASE, ts, ts + AV_TIME_BASE, 0);
                    while (video && !video_idle.load()) std::this_thread::sleep_for(std::chrono::microseconds(100));
                    while (audio && !audio_idle.load()) std::this_thread::sleep_for(std::chrono::microseconds(100));
                    while (::Frame::Video::receiver.try_receive());
                    while (::Frame::Audio::receiver.try_receive());
                    if (video_thrd.joinable()) video_thrd.join();
                    if (audio_thrd.joinable()) audio_thrd.join();
                    if (video) avcodec_flush_buffers(video->decoder);
                    if (audio) avcodec_flush_buffers(audio->decoder);
                    if (audio && audio->swr) audio->create_swr();
                    ::Packet::init_channels();
                    ::Frame::init_channels();
                    if (video) video_thrd = launch_video_decoder();
                    if (audio) audio_thrd = launch_audio_decoder();
                    seek_req.store(false);
                }

                if (video_decoder_status.load(std::memory_order::relaxed) < 0)
                    video = std::nullopt;
                if (audio_decoder_status.load(std::memory_order::relaxed) < 0)
                    audio = std::nullopt;

                unsigned long v = std::numeric_limits<unsigned long>::max();
                if (video) v = ::Packet::Video::sender.len();
                unsigned long a = std::numeric_limits<unsigned long>::max();
                if (audio) a = ::Packet::Audio::sender.len();
                if (v <= ::Packet::Video::SOFT_LIMIT ||
                    a <= ::Packet::Audio::SOFT_LIMIT) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        seek_available.store(false);

        ::Packet::Video::sender.drop_connection();
        ::Packet::Audio::sender.drop_connection();

        if (video_thrd.joinable()) video_thrd.join();
        if (audio_thrd.joinable()) audio_thrd.join();

        ::Frame::Video::sender.send_eof();
        ::Frame::Audio::sender.send_eof();
    });
}

std::thread Decoder::launch_video_decoder() {
    assert(video);
    return std::thread([this]() mutable {
        try {
            Frame f = av_frame_alloc();
            auto receive_franes = [&]() {
                while (avcodec_receive_frame(video->decoder, f.frame) >= 0) {
                    if (video->sws) {
                        Frame out = av_frame_alloc();
                        av_frame_copy_props(out.frame, f.frame);
                        int res = sws_scale_frame(video->sws, out.frame, f.frame);
                        if (res < 0) throw res;
                        av_frame_unref(f.frame);
                        out.frame->time_base = ictx->streams[video->stream_index]->time_base;
                        ::Frame::Video::sender.send(std::move(out));
                    } else {
                        f.frame->time_base = ictx->streams[video->stream_index]->time_base;
                        ::Frame::Video::sender.send(std::move(f));
                        f.frame = av_frame_alloc();
                    }

                    if (seek_req.load()) throw 0;
                }
            };

            while (auto rec = ::Packet::Video::receiver.receive()) {
                if (avcodec_send_packet(video->decoder, rec->packet) >= 0)
                    receive_franes();
            }

            int res = avcodec_send_packet(video->decoder, nullptr);
            if (res < 0) throw res;
            receive_franes();
        } catch (int err) {
            if (err < 0) {
                video_decoder_status.store(err, std::memory_order::relaxed);
                std::cerr << "Video decode error: " << err << "\n";
            }
            return;
        }
    });
}

std::thread Decoder::launch_audio_decoder() {
    assert(audio);
    return std::thread([this]() mutable {
        try {
            Frame f = av_frame_alloc();
            auto receive_franes = [&]() {
                while (avcodec_receive_frame(audio->decoder, f.frame) >= 0) {
                    if (audio->swr) {
                        auto res = resample_audio(f.frame);
                        if (!res) throw res.error();
                        av_frame_unref(f.frame);
                        res->frame->time_base = ictx->streams[video->stream_index]->time_base;
                        ::Frame::Audio::sender.send(std::move(*res));
                    } else {
                        f.frame->time_base = ictx->streams[video->stream_index]->time_base;
                        ::Frame::Audio::sender.send(std::move(f));
                        f = av_frame_alloc();
                    }

                    if (seek_req.load()) throw 0;
                }
            };

            while (auto rec = ::Packet::Audio::receiver.receive()) {
                if (avcodec_send_packet(audio->decoder, rec->packet) >= 0)
                    receive_franes();
            }

            int res = avcodec_send_packet(audio->decoder, nullptr);
            if (res < 0) throw res;
            receive_franes();

            if (audio->swr) {
                while (auto res = resample_audio(nullptr)) {
                    if (res->frame->nb_samples == 0) break;
                    ::Frame::Audio::sender.send(std::move(*res));
                }
            }
        } catch (int err) {
            if (err < 0) {
                audio_decoder_status.store(err, std::memory_order::relaxed);
                std::cerr << "Audio decode error: " << err << "\n";
            }
            return;
        }
    });
}

std::expected<Decoder::Frame, int> Decoder::resample_audio(const AVFrame* input) {
    Frame out = av_frame_alloc();
    av_frame_copy_props(out.frame, input);
    int64_t sample_rate;
    av_opt_get_int(audio->swr, "out_sample_rate", 0, &sample_rate);
    out.frame->sample_rate = static_cast<int>(sample_rate);
    av_opt_get_sample_fmt(audio->swr, "out_sample_fmt", 0, (AVSampleFormat*)&out.frame->format);
    av_opt_get_chlayout(audio->swr, "out_chlayout", 0, &out.frame->ch_layout);
    int res = swr_convert_frame(audio->swr, out.frame, input);
    if (res < 0) return std::unexpected(res);
    assert(out.frame);
    if (out.frame->nb_samples > 0) return out;
    return std::unexpected(0);
}

std::optional<Decoder::Stream> Decoder::new_stream(AVMediaType type) {
    const AVCodec* codec = nullptr;
    int index = av_find_best_stream(ictx, type, -1, -1, &codec, 0);
    if (index < 0 || codec == nullptr)
        return std::nullopt;
    AVCodecContext* decoder = avcodec_alloc_context3(codec);
    decoder->thread_count = 0;
    decoder->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (avcodec_parameters_to_context(decoder, ictx->streams[index]->codecpar) < 0 ||
        avcodec_open2(decoder, codec, nullptr) < 0) {
        avcodec_free_context(&decoder);
        return std::nullopt;
    }
    return Stream(index, codec, decoder);
}
