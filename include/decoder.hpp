#pragma once
#include <RtAudio.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <cstdint>
#include <expected>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
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

static const std::set<AVPixelFormat> supported_pixel_formats = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ420P,
};

class Decoder {
    AVFormatContext* ictx = nullptr;

    struct Stream {
        int stream_index = -1;
        const AVCodec* codec = nullptr;
        AVCodecContext* decoder = nullptr;
        ~Stream() { avcodec_free_context(&decoder); }
        Stream(int i, const AVCodec* c, AVCodecContext* d) : stream_index(i), codec(c), decoder(d) {}
        Stream(const Stream&) = delete;
        Stream(Stream&& o) noexcept : stream_index(o.stream_index), codec(o.codec), decoder(std::exchange(o.decoder, nullptr)) {}
    };

    struct VideoStream : Stream {
        SwsContext* sws = nullptr;
        VideoStream(Stream&& s) : Stream(std::move(s)) {
            if (supported_pixel_formats.contains(decoder->pix_fmt)) return;
            #ifndef NDEBUG
            std::cout << "using software scaling\n";
            #endif
            create_sws();
        }
        void create_sws() {
            sws_freeContext(sws);
            // clang-format off
            sws = sws_getContext(
                decoder->width, decoder->height, decoder->pix_fmt,
                decoder->width, decoder->height, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            // clang-format on
        }
        VideoStream(VideoStream&& o) noexcept : Stream(std::move(o)), sws(std::exchange(o.sws, nullptr)) {}
        ~VideoStream() { sws_freeContext(sws); }
    };

    struct AudioStream : Stream {
        SwrContext* swr = nullptr;
        AudioStream(Stream&& s) : Stream(std::move(s)) {
            if (supported_audio_formats.contains(decoder->sample_fmt)) return;
            #ifndef NDEBUG
            std::cout << "using software resampling\n";
            #endif
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

    std::optional<VideoStream> video = std::nullopt;
    std::optional<AudioStream> audio = std::nullopt;

   public:
    Decoder(const char* path, int skip = 0) {
        if (avformat_open_input(&ictx, path, nullptr, nullptr) < 0 ||
            avformat_find_stream_info(ictx, nullptr) < 0) {
            throw std::runtime_error("Cannot read input");
        }

        std::int64_t ts = static_cast<std::int64_t>(skip) * AV_TIME_BASE;
        if (ts > 0) avformat_seek_file(ictx, -1, ts, ts, ts, 0);

        if (auto strm = new_stream(AVMEDIA_TYPE_VIDEO)) video.emplace(std::move(*strm));
        if (auto strm = new_stream(AVMEDIA_TYPE_AUDIO)) audio.emplace(std::move(*strm));
    }

    void no_video() { video = std::nullopt; }
    void no_audio() { audio = std::nullopt; }

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

    bool has_video() const { return video.has_value(); }
    bool has_audio() const { return audio.has_value(); }

    // for video
    std::optional<int> input_width() const {
        if (!video) return std::nullopt;
        if (video->sws) return video->sws->src_w;
        return video->decoder->width;
    }

    std::optional<int> input_height() const {
        if (!video) return std::nullopt;
        if (video->sws) return video->sws->src_h;
        return video->decoder->height;
    }

    std::optional<AVPixelFormat> input_pixel_format() const {
        if (!video) return std::nullopt;
        if (video->sws) return static_cast<AVPixelFormat>(video->sws->src_format);
        return video->decoder->pix_fmt;
    }

    std::optional<int> output_width() const {
        if (!video) return std::nullopt;
        if (video->sws) return video->sws->dst_w;
        return video->decoder->width;
    }

    std::optional<int> output_height() const {
        if (!video) return std::nullopt;
        if (video->sws) return video->sws->dst_h;
        return video->decoder->height;
    }

    std::optional<AVPixelFormat> output_pixel_format() const {
        if (!video) return std::nullopt;
        if (video->sws) return static_cast<AVPixelFormat>(video->sws->dst_format);
        return video->decoder->pix_fmt;
    }

    std::optional<double> fps() const {
        if (video) return av_q2d(ictx->streams[video->stream_index]->avg_frame_rate);
        return std::nullopt;
    }

    std::optional<std::int64_t> duration_us() const {
        return ictx->duration;
    }

    // for audio
    std::optional<int> input_channels() const {
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

    std::optional<int> output_channels() const {
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
    std::optional<Stream> new_stream(AVMediaType type);

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
    std::thread launch_video_decoder();
    std::thread launch_audio_decoder();
    std::expected<Frame, int> resample_audio(const AVFrame* input);
};
