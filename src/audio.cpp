#include "audio.hpp"

#include <RtAudio.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "decoder.hpp"
#include "ringbuf.hpp"

template <typename T>
int Audio::callback(void* output_buffer, void* input_buffer, unsigned int nbuffer_frames,
                    double stream_time, RtAudioStreamStatus status, void* user_data) {
    (void)input_buffer;
    (void)stream_time;
    (void)status;

    Audio& self = *static_cast<Audio*>(user_data);
    T* buffer = static_cast<T*>(output_buffer);

    const std::size_t needed = static_cast<std::size_t>(nbuffer_frames) *
                               static_cast<std::size_t>(self.channels);

    while (self.queue<T>().size() < needed) {
        if (auto res = self.receiver.receive_timeout(std::chrono::milliseconds(1))) {
            auto* frame = res->frame;
            assert(frame);
            assert(static_cast<unsigned int>(frame->ch_layout.nb_channels) == self.channels);

            T* samples = reinterpret_cast<T*>(frame->data[0]);
            std::size_t len = static_cast<std::size_t>(frame->nb_samples) *
                              static_cast<std::size_t>(frame->ch_layout.nb_channels);

            self.queue<T>().append_range(std::span<T>(samples, len));
        } else
            break;
    }

    const std::size_t n = std::min(self.queue<T>().size(), needed);
    std::size_t i = 0;
    for (; i < n; i++) buffer[i] = self.queue<T>()[i];
    for (; i < needed; i++) buffer[i] = T();  // pad with silence
    self.queue<T>().erase(self.queue<T>().begin(), self.queue<T>().begin() + n);

    return 0;
}

Audio::Audio(unsigned int channels, unsigned int sample_rate, RtAudioFormat sample_format, RingBuf::Receiver<Decoder::Frame> receiver)
    : receiver(std::move(receiver)),
      channels(channels),
      sample_rate(sample_rate) {
    RtAudio::StreamParameters parameters;
    parameters.deviceId = dac.getDefaultOutputDevice();
    parameters.nChannels = channels;
    parameters.firstChannel = 0;
    unsigned int buffer_frames = 256;

    RtAudioCallback x;

    // clang-format off
        switch (sample_format) {
            case RTAUDIO_FLOAT32: x = callback<float>; queues = std::vector<float>(); break;
            case RTAUDIO_FLOAT64: x = callback<double>; queues = std::vector<double>(); break;
            case RTAUDIO_SINT8: x = callback<std::int8_t>; queues = std::vector<std::int8_t>(); break;
            case RTAUDIO_SINT16: x = callback<std::int16_t>; queues = std::vector<std::int16_t>(); break;
            case RTAUDIO_SINT32: x = callback<std::int32_t>; queues = std::vector<std::int32_t>(); break;
            default: throw std::runtime_error("Audio::Audio: Unknown sample format " + std::to_string(sample_format));
        }
    // clang-format on

    if (dac.openStream(&parameters, nullptr, sample_format, sample_rate, &buffer_frames, x, this)) {
        throw std::runtime_error(dac.getErrorText());
    }
}

Audio::~Audio() {
    if (dac.isStreamRunning())
        dac.stopStream();
    dac.closeStream();
}

void Audio::play() {
    if (dac.startStream())
        throw std::runtime_error(dac.getErrorText());
}
