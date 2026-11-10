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
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
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
#include <libavutil/rational.h>
#include <libswresample/swresample.h>
}

#include "state.hpp"

// hold an AVERROR code if < 0
static std::atomic_int video_decoder_status = 0;
static std::atomic_int audio_decoder_status = 0;

std::thread Decoder::launch() {
    return std::thread([this]() mutable {
        DecoderState state;
        state.init();
        video_decoder_status.store(0);
        audio_decoder_status.store(0);

        filter = std::make_shared<Filter>();

        std::jthread video_thrd, audio_thrd;
        if (video) video_thrd = launch_video_decoder();
        if (audio) audio_thrd = launch_audio_decoder();

        UniqueAVPacket packet(av_packet_alloc());
        while (video || audio) {
            if (av_read_frame(ictx, packet.get()) >= 0) {
                if (video && packet->stream_index == video->stream_index) {
                    if (state.send_video_packet(std::move(packet)) != std::nullopt)
                        video = nullptr;
                } else if (audio && packet->stream_index == audio->stream_index) {
                    if (state.send_audio_packet(std::move(packet)) != std::nullopt)
                        audio = nullptr;
                }
                packet = UniqueAVPacket(av_packet_alloc());
            } else {
                state.close_connection();
                bool should_quit = true;
                while (state.should_wait()) {
                    if (state.is_seek()) {
                        should_quit = false;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (should_quit) break;
            }

            while (video || audio) {
                if (state.is_seek()) {
                    state.handle_seek();

                    std::int64_t ts = state.current_us();
                    avformat_seek_file(ictx, -1, 0, ts, ts, 0);

                    if (video_thrd.joinable()) video_thrd.detach();
                    if (audio_thrd.joinable()) audio_thrd.detach();

                    if (auto strm = new_stream(AVMEDIA_TYPE_VIDEO)) video = std::make_shared<VideoStream>(std::move(*strm));
                    if (auto strm = new_stream(AVMEDIA_TYPE_AUDIO)) audio = std::make_shared<AudioStream>(std::move(*strm));
                    if (video) filter = std::make_shared<Filter>();
                    if (video) video_thrd = launch_video_decoder();
                    if (audio) audio_thrd = launch_audio_decoder();

                    break;
                }

                if (video_decoder_status.load(std::memory_order::relaxed) < 0)
                    video = nullptr;
                if (audio_decoder_status.load(std::memory_order::relaxed) < 0)
                    audio = nullptr;

                unsigned long v = std::numeric_limits<unsigned long>::max();
                if (video) v = state.video_packet_queue_len();
                unsigned long a = std::numeric_limits<unsigned long>::max();
                if (audio) a = state.audio_packet_queue_len();
                if (v <= 10 || a <= 10) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });
}

#define TRY(expr)                                                        \
    do {                                                                 \
        int ret;                                                         \
        if ((ret = expr) < 0) {                                          \
            video_decoder_status.store(ret, std::memory_order::relaxed); \
            std::cerr << "Video decode error: " << ret << "\n";          \
            return;                                                      \
        }                                                                \
    } while (0)

std::jthread Decoder::launch_video_decoder() const {
    assert(video);
    // capture by copy the current state of the decoder
    return std::jthread([this, video = video, filter = filter]() {
        SubDecoderVideoState state;
        state.init();

        UniqueAVFrame decoded(av_frame_alloc());
        auto receive_franes = [&]() {
            while (avcodec_receive_frame(video->decoder, decoded.get()) >= 0) {
                if (!filter->graph)
                    init_filters(*filter,
                                 *video,
                                 ictx->streams[video->stream_index]->time_base,
                                 filter_descr);

                std::int64_t target_ts = av_rescale_q(
                    state.current_us(),
                    AV_TIME_BASE_Q,
                    ictx->streams[video->stream_index]->time_base);

                decoded->pts = decoded->best_effort_timestamp;

                if (decoded->pts >= target_ts) {
                    TRY(av_buffersrc_add_frame_flags(filter->buffersrc_ctx, decoded.get(), AV_BUFFERSRC_FLAG_KEEP_REF));
                    while (1) {
                        UniqueAVFrame filtered(av_frame_alloc());
                        int res = av_buffersink_get_frame(filter->buffersink_ctx, filtered.get());
                        if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) break;
                        TRY(res);
                        filtered->time_base = ictx->streams[video->stream_index]->time_base;
                        if (!state.send(std::move(filtered))) return;
                    }
                }

                av_frame_unref(decoded.get());
            }
        };

        while (auto packet = state.recv()) {
            if (avcodec_send_packet(video->decoder, packet->get()) >= 0)
                receive_franes();
        }

        TRY(avcodec_send_packet(video->decoder, nullptr));
        receive_franes();

        TRY(av_buffersrc_add_frame_flags(filter->buffersrc_ctx, nullptr, 0));
        while (1) {
            UniqueAVFrame filtered(av_frame_alloc());
            int res = av_buffersink_get_frame(filter->buffersink_ctx, filtered.get());
            if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) break;
            TRY(res);
            filtered->time_base = ictx->streams[video->stream_index]->time_base;
            if (!state.send(std::move(filtered))) return;
        }
    });
}

#undef TRY
#define TRY(expr)                                                        \
    do {                                                                 \
        int ret;                                                         \
        if ((ret = expr) < 0) {                                          \
            audio_decoder_status.store(ret, std::memory_order::relaxed); \
            std::cerr << "Audio decode error: " << ret << "\n";          \
            return;                                                      \
        }                                                                \
    } while (0)

std::jthread Decoder::launch_audio_decoder() const {
    assert(audio);
    // capture by copy the current state of the decoder
    return std::jthread([this, audio = audio]() {
        SubDecoderAudioState state;
        state.init();

        UniqueAVFrame decoded(av_frame_alloc());
        auto receive_franes = [&]() {
            while (avcodec_receive_frame(audio->decoder, decoded.get()) >= 0) {
                std::int64_t target_ts = av_rescale_q(
                    state.current_us(),
                    AV_TIME_BASE_Q,
                    ictx->streams[audio->stream_index]->time_base);

                if (decoded->pts >= target_ts) {
                    if (audio->swr) {
                        UniqueAVFrame out(av_frame_alloc());
                        TRY(resample_audio(*audio, out, decoded));
                        if (!state.send(std::move(out))) return;
                    } else {
                        if (!state.send(std::move(decoded))) return;
                        decoded = UniqueAVFrame(av_frame_alloc());
                    }
                }

                av_frame_unref(decoded.get());
            }
        };

        while (auto packet = state.recv()) {
            if (avcodec_send_packet(audio->decoder, packet->get()) >= 0)
                receive_franes();
        }

        TRY(avcodec_send_packet(audio->decoder, nullptr));
        receive_franes();

        if (audio->swr) {
            while (1) {
                UniqueAVFrame out(av_frame_alloc());
                TRY(resample_audio(*audio, out, nullptr));
                if (out->nb_samples == 0) break;
                if (!state.send(std::move(out))) return;
            }
        }
    });
}

#undef TRY
#define TRY(expr)               \
    do {                        \
        int ret;                \
        if ((ret = expr) < 0) { \
            return ret;         \
        }                       \
    } while (0)

int Decoder::resample_audio(const Decoder::AudioStream& audio, UniqueAVFrame& output, const UniqueAVFrame& input) {
    int64_t sample_rate;
    TRY(av_opt_get_int(audio.swr, "out_sample_rate", 0, &sample_rate));
    output->sample_rate = static_cast<int>(sample_rate);
    TRY(av_opt_get_sample_fmt(audio.swr, "out_sample_fmt", 0, (AVSampleFormat*)&output->format));
    TRY(av_opt_get_chlayout(audio.swr, "out_chlayout", 0, &output->ch_layout));
    TRY(swr_convert_frame(audio.swr, output.get(), input.get()));
    return 0;
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
int Decoder::init_filters(Decoder::Filter& filter,
                          const Decoder::VideoStream& video,
                          AVRational time_base,
                          const std::string& filter_descr) {
    char args[512];
    int ret = 0;
    const AVFilter* buffersrc = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();

    filter.graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter.graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args,
             sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:colorspace=%s:range=%s",
             video.decoder->width,
             video.decoder->height,
             video.decoder->pix_fmt,
             time_base.num,
             time_base.den,
             video.decoder->sample_aspect_ratio.num,
             video.decoder->sample_aspect_ratio.den,
             av_color_space_name(video.decoder->colorspace),
             av_color_range_name(video.decoder->color_range));

    ret = avfilter_graph_create_filter(&filter.buffersrc_ctx, buffersrc, "in", args, NULL, filter.graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    filter.buffersink_ctx = avfilter_graph_alloc_filter(filter.graph, buffersink, "out");
    if (!filter.buffersink_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = av_opt_set(filter.buffersink_ctx, "pixel_formats", pixel_format, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }

    ret = avfilter_init_dict(filter.buffersink_ctx, NULL);
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
    outputs->filter_ctx = filter.buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name = av_strdup("out");
    inputs->filter_ctx = filter.buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    if ((ret = avfilter_graph_parse_ptr(filter.graph, filter_descr.c_str(), &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter.graph, NULL)) < 0)
        goto end;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}
