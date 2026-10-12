#pragma once
#include <RtAudio.h>

#include <cstdint>
#include <variant>
#include <vector>

#include "decoder.hpp"
#include "ringbuf.hpp"

class Audio {
    RtAudio dac;
    RingBuf::Receiver<Decoder::Frame> receiver;
    unsigned int channels;
    unsigned int sample_rate;

    std::variant<
        std::vector<float>,
        std::vector<double>,
        std::vector<std::int8_t>,
        std::vector<std::int16_t>,
        std::vector<std::int32_t>>
        queues;
    template <typename T>
    std::vector<T>& queue() { return std::get<std::vector<T>>(queues); }

   public:
    Audio(unsigned int channels, unsigned int sample_rate, RtAudioFormat sample_format, RingBuf::Receiver<Decoder::Frame> receiver);
    void play();
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    Audio(Audio&&) noexcept = default;
    Audio& operator=(Audio&&) noexcept = default;

   private:
    template <typename T>
    static int callback(void* output_buffer, void* input_buffer, unsigned int nbuffer_frames,
                        double stream_time, RtAudioStreamStatus status, void* user_data);
};
