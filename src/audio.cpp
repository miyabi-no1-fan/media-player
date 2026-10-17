#include "audio.hpp"

#include <RtAudio.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "state.hpp"

extern std::atomic_bool paused;
extern std::atomic_bool seek_req;
std::atomic_bool audio_idle = false;

template <typename T>
int Audio::callback(void* output_buffer, void*, unsigned int nbuffer_frames,
                    double, RtAudioStreamStatus, void* user_data) {
    Audio& self = *static_cast<Audio*>(user_data);
    T* buffer = static_cast<T*>(output_buffer);

    const std::size_t needed = static_cast<std::size_t>(nbuffer_frames) *
                               static_cast<std::size_t>(self.channels);

    if (paused.load()) {
        std::memset(buffer, 0, needed * sizeof(T));
        return 0;
    }

    const std::chrono::duration<double> time_budget(static_cast<double>(needed) / static_cast<double>(self.sample_rate));

    while (self.queue<T>().size() < needed) {
        auto res = Frame::Audio::receiver.receive_timeout(time_budget * 0.8);
        if (!res) break;
        auto* frame = res->frame;
        assert(frame);
        assert(static_cast<unsigned int>(frame->ch_layout.nb_channels) == self.channels);

        T* samples = reinterpret_cast<T*>(frame->data[0]);
        std::size_t len = static_cast<std::size_t>(frame->nb_samples) *
                          static_cast<std::size_t>(frame->ch_layout.nb_channels);

        self.queue<T>().append_range(std::span<T>(samples, len));

        if (seek_req.load()) {
            self.queue<T>().clear();
            audio_idle.store(true);
            while (seek_req.load()) std::this_thread::sleep_for(std::chrono::microseconds(100));
            audio_idle.store(false);
        }
    }

    const std::size_t n = std::min(self.queue<T>().size(), needed);
    std::memcpy(buffer, self.queue<T>().data(), n * sizeof(T));
    if (n < needed) std::memset(buffer + n, 0, (needed - n) * sizeof(T));
    self.queue<T>().erase(self.queue<T>().begin(), self.queue<T>().begin() + n);

    return 0;
}

Audio::Audio(unsigned int channels, unsigned int sample_rate, RtAudioFormat sample_format)
    : channels(channels),
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
