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
#pragma once
#include <RtAudio.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

static const std::map<AVSampleFormat, RtAudioFormat> supported_audio_formats = {
    { AV_SAMPLE_FMT_FLT, RTAUDIO_FLOAT32 },
    { AV_SAMPLE_FMT_DBL, RTAUDIO_FLOAT64 },
    { AV_SAMPLE_FMT_S16, RTAUDIO_SINT16 },
    { AV_SAMPLE_FMT_S32, RTAUDIO_SINT32 },
};

inline RtAudioFormat rt_audio_format(const AVSampleFormat& format) {
    return supported_audio_formats.find(format)->second;
}

// output pixels format for the FFmpeg filter
static constexpr const char* pixel_format = "yuv420p";

class Decoder {
    AVFormatContext* ictx = nullptr;

    std::string filter_descr;
    struct Filter {
        AVFilterGraph* graph = nullptr;
        AVFilterContext* buffersrc_ctx = nullptr;
        AVFilterContext* buffersink_ctx = nullptr;
        Filter() {}
        ~Filter() {
            avfilter_graph_free(&graph);
            buffersrc_ctx = nullptr;
            buffersink_ctx = nullptr;
        }
        Filter(const Filter&) = delete;
        Filter& operator=(const Filter&) = delete;
        Filter(Filter&& o) noexcept
            : graph(std::exchange(o.graph, nullptr)),
              buffersrc_ctx(std::exchange(o.buffersrc_ctx, nullptr)),
              buffersink_ctx(std::exchange(o.buffersink_ctx, nullptr)) {}
        Filter& operator=(Filter&& o) noexcept {
            if (this != &o) {
                avfilter_graph_free(&graph);
                graph = std::exchange(o.graph, nullptr);
                buffersrc_ctx = std::exchange(o.buffersrc_ctx, nullptr);
                buffersink_ctx = std::exchange(o.buffersink_ctx, nullptr);
            }
            return *this;
        }
    };

    struct Stream {
        int stream_index = -1;
        const AVCodec* codec = nullptr;
        AVCodecContext* decoder = nullptr;
        ~Stream() { avcodec_free_context(&decoder); }
        Stream() {}
        Stream(int i, const AVCodec* c, AVCodecContext* d) : stream_index(i), codec(c), decoder(d) {}
        Stream(const Stream&) = delete;
        Stream(Stream&& o) noexcept : stream_index(o.stream_index), codec(o.codec), decoder(std::exchange(o.decoder, nullptr)) {}
    };

    struct VideoStream : Stream {
        VideoStream(Stream&& s) : Stream(std::move(s)) {}
        VideoStream(VideoStream&& o) noexcept : Stream(std::move(o)) {}
    };

    struct AudioStream : Stream {
        SwrContext* swr = nullptr;
        AudioStream(Stream&& s) : Stream(std::move(s)) {
            if (supported_audio_formats.contains(decoder->sample_fmt)) return;
            create_swr();
        }
        void create_swr() {
            AVSampleFormat fmt;
            auto it = supported_audio_formats.find(
                av_sample_fmt_is_planar(decoder->sample_fmt)
                    ? av_get_packed_sample_fmt(decoder->sample_fmt)
                    : decoder->sample_fmt);
            if (it != supported_audio_formats.end()) fmt = it->first;
            else fmt = supported_audio_formats.begin()->first;

            swr_free(&swr);
            // clang-format off
            swr_alloc_set_opts2(&swr,
                &decoder->ch_layout, fmt, decoder->sample_rate,
                &decoder->ch_layout, decoder->sample_fmt, decoder->sample_rate,
                0, nullptr);
            // clang-format on
        }
        AudioStream(AudioStream&& o) noexcept : Stream(std::move(o)), swr(std::exchange(o.swr, nullptr)) {}
        ~AudioStream() { swr_free(&swr); }
    };

    std::shared_ptr<Filter> filter = nullptr;

    std::shared_ptr<VideoStream> video = nullptr;
    std::shared_ptr<AudioStream> audio = nullptr;

   public:
    Decoder(const char* path) {
        #ifdef NDEBUG
        av_log_set_level(AV_LOG_ERROR);
        #endif

        if (avformat_open_input(&ictx, path, nullptr, nullptr) < 0 ||
            avformat_find_stream_info(ictx, nullptr) < 0) {
            avformat_close_input(&ictx);
            throw std::runtime_error("Cannot read input file: " + std::string(path));
        }

        if (auto strm = new_stream(AVMEDIA_TYPE_VIDEO)) video = std::make_shared<VideoStream>(std::move(*strm));
        if (auto strm = new_stream(AVMEDIA_TYPE_AUDIO)) audio = std::make_shared<AudioStream>(std::move(*strm));
        discard_unused_streams({ AVMEDIA_TYPE_VIDEO, AVMEDIA_TYPE_AUDIO });

        if (video) {
            bool has_subtitle = false;
            for (unsigned i = 0; i < ictx->nb_streams; i++) {
                has_subtitle = ictx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE;
                if (has_subtitle) break;
            }
            if (has_subtitle) {
                filter_descr.resize(32 + std::strlen(path));
                std::snprintf(filter_descr.data(), filter_descr.size(), "subtitles=filename='%s'", path);
            } else {
                filter_descr = "null";
            }
        }
    }

    void no_video() { video = nullptr; }
    void no_audio() { audio = nullptr; }

    struct Frame {
        AVFrame* frame = nullptr;
        Frame() { frame = nullptr; }
        Frame(AVFrame* f) : frame(f) {}
        ~Frame() { av_frame_free(&frame); }
        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;
        Frame(Frame&& o) {
            frame = o.frame;
            o.frame = nullptr;
        }
        Frame& operator=(Frame&& o) {
            if (frame != nullptr) {
                av_frame_free(&frame);
            }
            frame = o.frame;
            o.frame = nullptr;
            return *this;
        }
    };

    std::thread launch();

    ~Decoder() {
        avformat_close_input(&ictx);
    }

    bool has_video() const { return video != nullptr; }
    bool has_audio() const { return audio != nullptr; }

    // for video
    std::optional<int> width() const {
        if (!video) return std::nullopt;
        return video->decoder->width;
    }

    std::optional<int> height() const {
        if (!video) return std::nullopt;
        return video->decoder->height;
    }

    std::optional<AVPixelFormat> input_pixel_format() const {
        if (!video) return std::nullopt;
        return video->decoder->pix_fmt;
    }

    std::optional<AVPixelFormat> output_pixel_format() const {
        if (!video) return std::nullopt;
        static_assert(std::string_view(pixel_format) == "yuv420p");
        return AV_PIX_FMT_YUV420P;
    }

    std::optional<double> fps() const {
        if (video) return av_q2d(ictx->streams[video->stream_index]->avg_frame_rate);
        return std::nullopt;
    }

    std::int64_t duration_us() const {
        return ictx->duration;
    }

    // for audio
    std::optional<int> input_audio_channels() const {
        if (audio) return audio->decoder->ch_layout.nb_channels;
        return std::nullopt;
    }

    std::optional<int> input_sample_rate() const {
        if (audio) return audio->decoder->sample_rate;
        return std::nullopt;
    }

    std::optional<AVSampleFormat> input_sample_format() const {
        if (audio) return audio->decoder->sample_fmt;
        return std::nullopt;
    }

    std::optional<int> output_audio_channels() const {
        if (!audio) return std::nullopt;
        if (audio->swr) {
            AVChannelLayout channel_layout;
            av_opt_get_chlayout(audio->swr, "out_chlayout", 0, &channel_layout);
            int channels = channel_layout.nb_channels;
            av_channel_layout_uninit(&channel_layout);
            return channels;
        }
        return audio->decoder->ch_layout.nb_channels;
    }

    std::optional<int> output_sample_rate() const {
        if (!audio) return std::nullopt;
        if (audio) {
            int64_t sample_rate;
            av_opt_get_int(audio->swr, "out_sample_rate", 0, &sample_rate);
            return static_cast<int>(sample_rate);
        }
        return audio->decoder->sample_rate;
    }

    std::optional<AVSampleFormat> output_sample_format() const {
        if (!audio) return std::nullopt;
        if (audio) {
            AVSampleFormat fmt;
            av_opt_get_sample_fmt(audio->swr, "out_sample_fmt", 0, &fmt);
            return fmt;
        }
        return audio->decoder->sample_fmt;
    }

   private:
    std::optional<Stream> new_stream(AVMediaType type) const;
    void discard_unused_streams(const std::set<AVMediaType> used_streams);

   public:
    struct Packet {
        AVPacket* packet = nullptr;
        Packet() { packet = nullptr; }
        Packet(AVPacket* p) : packet(p) {}
        ~Packet() { av_packet_free(&packet); }
        Packet(const Packet&) = delete;
        Packet& operator=(const Packet&) = delete;
        Packet(Packet&& o) {
            packet = o.packet;
            o.packet = nullptr;
        }
        Packet& operator=(Packet&& o) noexcept {
            av_packet_free(&packet);
            packet = o.packet;
            o.packet = nullptr;
            return *this;
        }
    };

   private:
    int init_filters() const;
    std::thread launch_video_decoder() const;
    std::thread launch_audio_decoder() const;
    Frame resample_audio(const AVFrame* input) const;
};
