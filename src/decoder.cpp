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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/defs.h>
#include <libavcodec/packet.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libswresample/swresample.h>
}

#include "state.hpp"

std::jthread Decoder::launch() {
    return std::jthread([this](std::stop_token should_stop) mutable {
        DecoderState state;
        state.init();

        filter = std::make_shared<Filter>();

        std::jthread video_thrd, audio_thrd;
        video_thrd = launch_video_decoder();
        audio_thrd = launch_audio_decoder();

        UniqueAVPacket packet(av_packet_alloc());
        while (video || audio) {
            if (av_read_frame(ictx, packet.get()) >= 0) {
                if (false) std::unreachable();
                else if (video && packet->stream_index == video->stream_index) state.send_video_packet(std::move(packet));
                else if (audio && packet->stream_index == audio->stream_index) state.send_audio_packet(std::move(packet));
                packet = UniqueAVPacket(av_packet_alloc());
            } else {
                state.close_connection();
                bool should_quit = true;
                while (!should_stop.stop_requested()) {
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

                    std::int64_t ts = state.current_us() + start_time_us();
                    avformat_seek_file(ictx, -1, 0, ts, ts, 0);

                    if (video_thrd.joinable()) {
                        video_thrd.request_stop();
                        video_thrd.detach();
                    }
                    if (audio_thrd.joinable()) {
                        audio_thrd.request_stop();
                        audio_thrd.detach();
                    }

                    if (video)
                        if (auto strm = new_stream(AVMEDIA_TYPE_VIDEO))
                            video = std::make_shared<VideoStream>(std::move(*strm));
                    if (audio)
                        if (auto strm = new_stream(AVMEDIA_TYPE_AUDIO))
                            audio = std::make_shared<AudioStream>(std::move(*strm));

                    if (video) filter = std::make_shared<Filter>();

                    video_thrd = launch_video_decoder();
                    audio_thrd = launch_audio_decoder();

                    break;
                }

                if (state.sub_decode_video_status() < 0) video = nullptr;
                if (state.sub_decode_audio_status() < 0) audio = nullptr;

                unsigned long v = std::numeric_limits<unsigned long>::max();
                if (video) v = state.video_packet_queue_len();
                unsigned long a = std::numeric_limits<unsigned long>::max();
                if (audio) a = state.audio_packet_queue_len();
                if (v <= VIDEO_PACKET_QUEUE_SOFT_LIMIT || a <= AUDIO_PACKET_QUEUE_SOFT_LIMIT) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });
}

#define TRY(expr)                                               \
    do {                                                        \
        int ret;                                                \
        if ((ret = expr) < 0) {                                 \
            state.error(ret);                                   \
            std::cerr << "Video decode error: " << ret << "\n"; \
            return;                                             \
        }                                                       \
    } while (0)

std::jthread Decoder::launch_video_decoder() const {
    // capture by copy the current state of the decoder
    return std::jthread([video = video, filter = filter, filter_descr = filter_descr, start_time_us = start_time_us(),
                         time_base = video ? ictx->streams[video->stream_index]->time_base : AVRational()](std::stop_token should_stop) {
        SubDecoderVideoState state;
        state.init();
        if (!video) return;

        UniqueAVFrame decoded(av_frame_alloc());
        auto receive_franes = [&]() {
            while (avcodec_receive_frame(video->decoder, decoded.get()) >= 0) {
                if (!filter->graph) init_filters(*filter, *video, time_base, filter_descr);

                std::int64_t target_pts = av_rescale_q(state.current_us() + start_time_us,
                                                       AV_TIME_BASE_Q, time_base);

                decoded->pts = decoded->best_effort_timestamp;

                if (decoded->pts >= target_pts) {
                    TRY(av_buffersrc_add_frame_flags(filter->buffersrc_ctx, decoded.get(), AV_BUFFERSRC_FLAG_KEEP_REF));
                    while (1) {
                        UniqueAVFrame filtered(av_frame_alloc());
                        int res = av_buffersink_get_frame(filter->buffersink_ctx, filtered.get());
                        if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) break;
                        TRY(res);
                        filtered->time_base = time_base;
                        filtered->pts = filtered->best_effort_timestamp;
                        filtered->pts -= av_rescale_q(start_time_us, AV_TIME_BASE_Q, time_base);
                        if (state.send(std::move(filtered)) != std::nullopt) return;
                        if (should_stop.stop_requested()) return;
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
            filtered->time_base = time_base;
            filtered->pts = filtered->best_effort_timestamp;
            filtered->pts -= av_rescale_q(start_time_us, AV_TIME_BASE_Q, time_base);
            if (state.send(std::move(filtered)) != std::nullopt) return;
            if (should_stop.stop_requested()) return;
        }
    });
}

#undef TRY
#define TRY(expr)                                               \
    do {                                                        \
        int ret;                                                \
        if ((ret = expr) < 0) {                                 \
            state.error(ret);                                   \
            std::cerr << "Audio decode error: " << ret << "\n"; \
            return;                                             \
        }                                                       \
    } while (0)

std::jthread Decoder::launch_audio_decoder() const {
    // capture by copy the current state of the decoder
    return std::jthread([audio = audio, start_time_us = start_time_us(),
                         time_base = audio ? ictx->streams[audio->stream_index]->time_base : AVRational()](std::stop_token should_stop) {
        SubDecoderAudioState state;
        state.init();
        if (!audio) return;

        UniqueAVFrame decoded(av_frame_alloc());
        auto receive_franes = [&]() {
            while (avcodec_receive_frame(audio->decoder, decoded.get()) >= 0) {
                std::int64_t target_pts = av_rescale_q(state.current_us() + start_time_us,
                                                       AV_TIME_BASE_Q, time_base);

                if (decoded->pts >= target_pts) {
                    if (audio->swr) {
                        UniqueAVFrame out(av_frame_alloc());
                        TRY(resample_audio(*audio, out, decoded));
                        if (state.send(std::move(out)) != std::nullopt) return;
                    } else {
                        if (state.send(std::move(decoded)) != std::nullopt) return;
                        decoded = UniqueAVFrame(av_frame_alloc());
                    }
                }

                av_frame_unref(decoded.get());

                if (should_stop.stop_requested()) return;
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
                if (state.send(std::move(out)) != std::nullopt) return;
                if (should_stop.stop_requested()) return;
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

static AVPixelFormat _hw_pix_fmt;

static AVPixelFormat get_hw_format(AVCodecContext*, const enum AVPixelFormat* pix_fmts) {
    const enum AVPixelFormat* p;
    for (p = pix_fmts; *p != -1; p++) {
        if (*p == _hw_pix_fmt)
            return *p;
    }
    throw std::runtime_error("Failed to get HW surface format.");
    return AV_PIX_FMT_NONE;
}

bool Decoder::init_hw_decoder(const AVCodec* codec, AVHWDeviceType type) {
    for (int i = 0;; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
        if (!config) return false;
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == type) {
            hw_pix_fmt = config->pix_fmt;
            _hw_pix_fmt = hw_pix_fmt;
            break;
        }
    }

    if (av_hwdevice_ctx_create(&hw_device_ctx, type, NULL, NULL, 0) < 0) {
        hw_device_ctx = nullptr;
        return false;
    }

    return true;
}

std::optional<Decoder::Stream> Decoder::new_stream(AVMediaType type) {
    const AVCodec* codec = nullptr;
    int index = av_find_best_stream(ictx, type, -1, -1, &codec, 0);
    if (index < 0 || codec == nullptr)
        return std::nullopt;

    AVCodecContext* decoder = avcodec_alloc_context3(codec);

    if (avcodec_parameters_to_context(decoder, ictx->streams[index]->codecpar) < 0) {
        avcodec_free_context(&decoder);
        return std::nullopt;
    }

    if (type == AVMEDIA_TYPE_VIDEO) {
        if (!hw_device_ctx && hardware_accel_feature) {
            AVHWDeviceType hw_dev_type = AV_HWDEVICE_TYPE_NONE;
            while ((hw_dev_type = av_hwdevice_iterate_types(hw_dev_type)) != AV_HWDEVICE_TYPE_NONE) {
                if (init_hw_decoder(codec, hw_dev_type)) {
                    break;
                }
            }

            if (!hw_device_ctx) {
                #ifndef NDEBUG
                std::cout << "No hardware decoding available for this media" << std::endl;
                #endif
                hardware_accel_feature = false;
            }
        }

        if (hw_device_ctx) {
            decoder->hw_device_ctx = av_buffer_ref(hw_device_ctx);
            decoder->get_format = get_hw_format;
        } else {
            decoder->thread_count = 4;
            decoder->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        }
    }

    if (avcodec_open2(decoder, codec, nullptr) < 0) {
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

void Decoder::init_filter_descr(const std::string path) {
    std::vector<std::string> descrs;

    if (hw_device_ctx) descrs.push_back("hwdownload");

    descrs.push_back("format=pix_fmts=yuv420p|nv12");

    if (video) {
        bool has_subtitle = false;
        for (unsigned i = 0; i < ictx->nb_streams; i++) {
            has_subtitle = ictx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE;
            if (has_subtitle) break;
        }
        if (has_subtitle) descrs.push_back("subtitles=filename='" + path + "'");
    }

    for (std::size_t i = 0; i < descrs.size(); i++) {
        filter_descr += descrs[i];
        if (i != descrs.size() - 1) filter_descr += ",";
    }
}

int Decoder::init_filters(Decoder::Filter& filter,
                          const Decoder::VideoStream& video,
                          AVRational time_base,
                          const std::string& filter_descr) {
    int ret = 0;
    const AVFilter* buffersrc = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    AVBufferSrcParameters* par = nullptr;

    filter.graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter.graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    filter.buffersrc_ctx = avfilter_graph_alloc_filter(filter.graph, buffersrc, "in");
    if (!filter.buffersrc_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Cannot allocate buffer source\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    par = av_buffersrc_parameters_alloc();
    if (!par) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    par->width = video.decoder->width;
    par->height = video.decoder->height;
    par->format = video.decoder->pix_fmt;
    par->time_base = time_base;
    par->color_range = video.decoder->color_range;
    par->color_space = video.decoder->colorspace;
    par->alpha_mode = video.decoder->alpha_mode;
    par->frame_rate = video.decoder->framerate;
    if (video.decoder->hw_frames_ctx) {
        par->hw_frames_ctx = av_buffer_ref(video.decoder->hw_frames_ctx);
        if (!par->hw_frames_ctx) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
    }

    ret = av_buffersrc_parameters_set(filter.buffersrc_ctx, par);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set buffer source parameters\n");
        goto end;
    }

    ret = avfilter_init_str(filter.buffersrc_ctx, nullptr);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot initialize buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    filter.buffersink_ctx = avfilter_graph_alloc_filter(filter.graph, buffersink, "out");
    if (!filter.buffersink_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        ret = AVERROR(ENOMEM);
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
    if (par) {
        av_buffer_unref(&par->hw_frames_ctx);
        av_freep(&par);
    }
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}
