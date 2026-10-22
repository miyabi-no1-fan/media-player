#include "audio.hpp"

#include <RtAudio.h>
extern "C" {
#include <libavutil/avutil.h>
}
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "state.hpp"

template <typename T>
int Audio::callback(void* output_buffer, void*, unsigned int nbuffer_frames,
                    double, RtAudioStreamStatus, void* user_data) {
    Audio& self = *static_cast<Audio*>(user_data);
    T* buffer = static_cast<T*>(output_buffer);

    const std::size_t needed = static_cast<std::size_t>(nbuffer_frames) *
                               static_cast<std::size_t>(self.channels);
    const std::int64_t sample_time_us = (static_cast<std::int64_t>(nbuffer_frames) * AV_TIME_BASE) /
                                        static_cast<std::int64_t>(self.sample_rate);

    if (paused.load()) {
        std::memset(buffer, 0, needed * sizeof(T));
        return 0;
    }

    self.current_us->fetch_add(sample_time_us);

    while (self.queue<T>().size() < needed) {
        auto res = self.receiver->receive_timeout(std::chrono::microseconds(sample_time_us) * 0.8);

        if (audio_seek_req.load()) {
            // flush
            self.queue<T>().clear();
            // create new state
            self.new_state();
            // update clock
            self.current_us->store(seek_time_us.load());
            // seek done
            audio_seek_req.store(false);
            audio_seek_req.notify_all();
            continue;
        }

        if (!res) break;

        auto* frame = res->frame;
        assert(frame);
        assert(static_cast<unsigned int>(frame->ch_layout.nb_channels) == self.channels);

        T* samples = reinterpret_cast<T*>(frame->data[0]);
        std::size_t len = static_cast<std::size_t>(frame->nb_samples) *
                          static_cast<std::size_t>(frame->ch_layout.nb_channels);

        self.queue<T>().append_range(std::span<T>(samples, len));
    }

    const std::size_t n = std::min(self.queue<T>().size(), needed);
    std::memcpy(buffer, self.queue<T>().data(), n * sizeof(T));
    if (n < needed) std::memset(buffer + n, 0, (needed - n) * sizeof(T));
    self.queue<T>().erase(self.queue<T>().begin(), self.queue<T>().begin() + n);

    return 0;
}

Audio::Audio(unsigned int channels, unsigned int sample_rate, RtAudioFormat sample_format)
    : dac(RtAudio::UNSPECIFIED, [](RtAudioErrorType, const std::string& msg) {
          #ifdef NDEBUG
          (void)msg;
          #else
          std::cerr << msg << std::endl;
          #endif
      }),
      channels(channels),
      sample_rate(sample_rate) {
    RtAudio::StreamParameters parameters;
    parameters.deviceId = dac.getDefaultOutputDevice();
    parameters.nChannels = channels;
    parameters.firstChannel = 0;
    unsigned int buffer_frames = 512;

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
    new_state();
    if (dac.startStream())
        throw std::runtime_error(dac.getErrorText());
}

void Audio::new_state() {
    audio_current_us = std::make_shared<std::atomic_int64_t>(0);
    current_us = audio_current_us;
    // wait til channels are valid
    decoder_seek_req.wait(true);
    receiver = Frame::Audio::receiver;
}
