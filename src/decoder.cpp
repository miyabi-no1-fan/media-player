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
#include "decoder.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/defs.h>
#include <libavcodec/packet.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
}

#include "state.hpp"

// hold an AVERROR code if < 0
static std::atomic_int video_decoder_status = 0;
static std::atomic_int audio_decoder_status = 0;

static std::shared_ptr<std::atomic_bool> sub_decoders_should_quit = nullptr;

std::thread Decoder::launch() {
    return std::thread([this]() mutable {
        video_decoder_status.store(0);
        audio_decoder_status.store(0);
        seek_available.store(true);
        sub_decoders_should_quit = std::make_shared<std::atomic_bool>(false);

        filter = std::make_shared<Filter>();

        std::thread video_thrd, audio_thrd;
        if (video) video_thrd = launch_video_decoder();
        if (audio) audio_thrd = launch_audio_decoder();

        Packet p = av_packet_alloc();
        while (video || audio) {
            if (av_read_frame(ictx, p.packet) >= 0) {
                if (video && p.packet->stream_index == video->stream_index)
                    ::Packet::Video::sender->send(std::move(p));
                else if (audio && p.packet->stream_index == audio->stream_index)
                    ::Packet::Audio::sender->send(std::move(p));
                p = av_packet_alloc();
            } else {
                ::Packet::Video::sender->drop_connection();
                ::Packet::Audio::sender->drop_connection();
                bool should_quit = true;
                audio_seek_req.wait(true);
                while (audio_current_us->load() < duration_us()) {
                    if (decoder_seek_req.load()) {
                        should_quit = false;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                if (should_quit) break;
            }

            while (video || audio) {
                if (decoder_seek_req.load()) {
                    // seek frame
                    std::int64_t ts = seek_time_us.load();
                    avformat_seek_file(ictx, -1, 0, ts, ts, 0);

                    // tell sub-decode threads to quit -- this is flush
                    sub_decoders_should_quit->store(true);
                    if (video_thrd.joinable()) video_thrd.detach();
                    if (audio_thrd.joinable()) audio_thrd.detach();

                    /* sub-decode threads capture state by `shared_ptr`s
                    we just need to create new `shared_ptr`s and let
                    the old sub decoders threads die with the old state. */

                    // create new state
                    sub_decoders_should_quit = std::make_shared<std::atomic_bool>(false);
                    if (auto strm = new_stream(AVMEDIA_TYPE_VIDEO)) video = std::make_shared<VideoStream>(std::move(*strm));
                    if (auto strm = new_stream(AVMEDIA_TYPE_AUDIO)) audio = std::make_shared<AudioStream>(std::move(*strm));
                    if (video) filter = std::make_shared<Filter>();
                    ::Packet::new_channels();
                    ::Frame::new_channels();
                    if (video) video_thrd = launch_video_decoder();
                    if (audio) audio_thrd = launch_audio_decoder();

                    // seek done
                    decoder_seek_req.store(false);
                    decoder_seek_req.notify_all();
                    break;
                }

                if (video_decoder_status.load(std::memory_order::relaxed) < 0)
                    video = nullptr;
                if (audio_decoder_status.load(std::memory_order::relaxed) < 0)
                    audio = nullptr;

                unsigned long v = std::numeric_limits<unsigned long>::max();
                if (video) v = ::Packet::Video::sender->len();
                unsigned long a = std::numeric_limits<unsigned long>::max();
                if (audio) a = ::Packet::Audio::sender->len();
                if (v <= ::Packet::Video::SOFT_LIMIT ||
                    a <= ::Packet::Audio::SOFT_LIMIT) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        seek_available.store(false);

        if (video) ::Packet::Video::sender->drop_connection();
        if (audio) ::Packet::Audio::sender->drop_connection();

        if (video_thrd.joinable()) video_thrd.join();
        if (audio_thrd.joinable()) audio_thrd.join();

        if (video) ::Frame::Video::sender->send_eof();
        if (audio) ::Frame::Audio::sender->send_eof();
    });
}

#define TRY(expr)                        \
    do {                                 \
        int ret;                         \
        if ((ret = expr) < 0) throw ret; \
    } while (0)

std::thread Decoder::launch_video_decoder() const {
    assert(video);
    // capture by copy the current state of the decoder
    return std::thread([this,
                        video = video,
                        sender = ::Frame::Video::sender,
                        receiver = ::Packet::Video::receiver,
                        filter = filter,
                        should_quit = sub_decoders_should_quit]() {
        try {
            auto send = [this, &video, &sender, &should_quit](Frame& v) {
                v.frame->time_base = ictx->streams[video->stream_index]->time_base;
                Frame out = av_frame_alloc();
                av_frame_ref(out.frame, v.frame);
                while (1) {
                    auto r = sender->try_send(std::move(out));
                    if (!r) break;
                    out = std::move(*r);
                    if (should_quit->load()) throw 0;
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
                av_frame_unref(v.frame);
            };

            Frame decoded = av_frame_alloc();
            auto receive_franes = [&]() {
                while (avcodec_receive_frame(video->decoder, decoded.frame) >= 0) {
                    if (!filter->graph) init_filters();

                    std::int64_t target_us;
                    if (!audio_seek_req.load()) target_us = audio_current_us->load();
                    else target_us = seek_time_us.load();

                    std::int64_t target_ts = av_rescale_q(
                        target_us,
                        AV_TIME_BASE_Q,
                        ictx->streams[video->stream_index]->time_base);

                    decoded.frame->pts = decoded.frame->best_effort_timestamp;

                    if (decoded.frame->pts >= target_ts) {
                        TRY(av_buffersrc_add_frame_flags(filter->buffersrc_ctx, decoded.frame, AV_BUFFERSRC_FLAG_KEEP_REF));
                        Frame filtered = av_frame_alloc();
                        av_frame_copy_props(filtered.frame, decoded.frame);
                        while (1) {
                            int res = av_buffersink_get_frame(filter->buffersink_ctx, filtered.frame);
                            if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) break;
                            TRY(res);
                            send(filtered);
                        }
                    }

                    av_frame_unref(decoded.frame);
                }
            };

            while (1) {
                auto rec = receiver->try_receive();
                if (const auto& pkt = rec) {
                    if (avcodec_send_packet(video->decoder, pkt->packet) >= 0)
                        receive_franes();
                } else {
                    if (rec.error() == SPSC::Error::EndOfInput) break;
                    else std::this_thread::sleep_for(std::chrono::microseconds(100));
                }

                if (should_quit->load()) throw 0;
            }

            TRY(avcodec_send_packet(video->decoder, nullptr));
            receive_franes();

            TRY(av_buffersrc_add_frame_flags(filter->buffersrc_ctx, nullptr, 0));
            Frame filtered = av_frame_alloc();
            av_frame_copy_props(filtered.frame, decoded.frame);
            while (1) {
                int res = av_buffersink_get_frame(filter->buffersink_ctx, filtered.frame);
                if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) break;
                TRY(res);
                send(filtered);
            }
        } catch (int err) {
            if (err < 0) {
                video_decoder_status.store(err, std::memory_order::relaxed);
                std::cerr << "Video decode error: " << err << "\n";
            }
            return;
        }
    });
}

std::thread Decoder::launch_audio_decoder() const {
    assert(audio);
    // capture by copy the current state of the decoder
    return std::thread([this,
                        audio = audio,
                        sender = ::Frame::Audio::sender,
                        receiver = ::Packet::Audio::receiver,
                        should_quit = sub_decoders_should_quit]() {
        try {
            auto send = [this, &sender, &audio, &should_quit](Frame o) {
                while (1) {
                    o.frame->time_base = ictx->streams[audio->stream_index]->time_base;
                    auto r = sender->try_send(std::move(o));
                    if (!r) break;
                    o = std::move(*r);
                    if (should_quit->load()) throw 0;
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            };

            Frame decoded = av_frame_alloc();
            auto receive_franes = [&]() {
                while (avcodec_receive_frame(audio->decoder, decoded.frame) >= 0) {
                    std::int64_t target_us;
                    if (!audio_seek_req.load()) target_us = audio_current_us->load();
                    else target_us = seek_time_us.load();

                    std::int64_t target_ts = av_rescale_q(
                        target_us,
                        AV_TIME_BASE_Q,
                        ictx->streams[audio->stream_index]->time_base);

                    decoded.frame->pts = decoded.frame->best_effort_timestamp;

                    if (decoded.frame->pts >= target_ts) {
                        if (audio->swr) {
                            auto out = resample_audio(decoded.frame);
                            out.frame->pts = decoded.frame->best_effort_timestamp;
                            send(std::move(out));
                        } else {
                            send(std::move(decoded));
                        }
                    }

                    if (decoded.frame) av_frame_unref(decoded.frame);
                    else decoded = av_frame_alloc();
                }
            };

            while (1) {
                auto rec = receiver->try_receive();
                if (const auto& pkt = rec) {
                    if (avcodec_send_packet(audio->decoder, pkt->packet) >= 0)
                        receive_franes();
                } else {
                    if (rec.error() == SPSC::Error::EndOfInput) break;
                    else std::this_thread::sleep_for(std::chrono::microseconds(100));
                }

                if (should_quit->load()) throw 0;
            }

            TRY(avcodec_send_packet(audio->decoder, nullptr));
            receive_franes();

            if (audio->swr) {
                while (1) {
                    auto out = resample_audio(nullptr);
                    out.frame->pts = decoded.frame->best_effort_timestamp;
                    if (out.frame->nb_samples == 0) break;
                    send(std::move(out));
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

Decoder::Frame Decoder::resample_audio(const AVFrame* input) const {
    Frame out = av_frame_alloc();
    int64_t sample_rate;
    TRY(av_opt_get_int(audio->swr, "out_sample_rate", 0, &sample_rate));
    out.frame->sample_rate = static_cast<int>(sample_rate);
    TRY(av_opt_get_sample_fmt(audio->swr, "out_sample_fmt", 0, (AVSampleFormat*)&out.frame->format));
    TRY(av_opt_get_chlayout(audio->swr, "out_chlayout", 0, &out.frame->ch_layout));
    TRY(swr_convert_frame(audio->swr, out.frame, input));
    return out;
}

std::optional<Decoder::Stream> Decoder::new_stream(AVMediaType type) const {
    const AVCodec* codec = nullptr;
    int index = av_find_best_stream(ictx, type, -1, -1, &codec, 0);
    if (index < 0 || codec == nullptr)
        return std::nullopt;
    AVCodecContext* decoder = avcodec_alloc_context3(codec);
    if (type == AVMEDIA_TYPE_VIDEO) {
        decoder->thread_count = 4;
        decoder->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }
    if (avcodec_parameters_to_context(decoder, ictx->streams[index]->codecpar) < 0 ||
        avcodec_open2(decoder, codec, nullptr) < 0) {
        avcodec_free_context(&decoder);
        return std::nullopt;
    }
    return Stream(index, codec, decoder);
}

void Decoder::discard_unused_streams(const std::set<AVMediaType> used_streams) {
    for (unsigned i = 0; i < ictx->nb_streams; i++) {
        AVStream* strm = ictx->streams[i];
        if (!used_streams.contains(strm->codecpar->codec_type)) {
            strm->discard = AVDISCARD_ALL;
        }
    }
}

// copy from https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/decode_filter_video.c
int Decoder::init_filters() const {
    char args[512];
    int ret = 0;
    const AVFilter* buffersrc = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    AVRational time_base = ictx->streams[video->stream_index]->time_base;

    filter->graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter->graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args,
             sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:colorspace=%s:range=%s",
             video->decoder->width,
             video->decoder->height,
             video->decoder->pix_fmt,
             time_base.num,
             time_base.den,
             video->decoder->sample_aspect_ratio.num,
             video->decoder->sample_aspect_ratio.den,
             av_color_space_name(video->decoder->colorspace),
             av_color_range_name(video->decoder->color_range));

    ret = avfilter_graph_create_filter(&filter->buffersrc_ctx, buffersrc, "in", args, NULL, filter->graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    filter->buffersink_ctx = avfilter_graph_alloc_filter(filter->graph, buffersink, "out");
    if (!filter->buffersink_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = av_opt_set(filter->buffersink_ctx, "pixel_formats", pixel_format, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }

    ret = avfilter_init_dict(filter->buffersink_ctx, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot initialize buffer sink\n");
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name = av_strdup("in");
    outputs->filter_ctx = filter->buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name = av_strdup("out");
    inputs->filter_ctx = filter->buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    if ((ret = avfilter_graph_parse_ptr(filter->graph, filter_descr.c_str(), &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter->graph, NULL)) < 0)
        goto end;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}
