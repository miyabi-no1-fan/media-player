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
#include "audio.hpp"

#include <RtAudio.h>
extern "C" {
#include <libavutil/avutil.h>
}
#include <cassert>
#include <chrono>
#include <condition_variable>
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

    int return_code = 0;
    auto start = std::chrono::steady_clock::now();

    const std::size_t needed = static_cast<std::size_t>(nbuffer_frames) *
                               static_cast<std::size_t>(self.channels);
    const std::int64_t sample_time_us = (static_cast<std::int64_t>(nbuffer_frames) * AV_TIME_BASE) /
                                        static_cast<std::int64_t>(self.sample_rate);

    const auto deadline = start + std::chrono::microseconds(sample_time_us);

    if (self.state.is_paused()) {
        std::memset(buffer, 0, needed * sizeof(T));
        return 0;
    }

    while (self.queue<T>().size() < needed) {
        auto res = self.state.recv_deadline(deadline);

        if (!res) {
            switch (res.error()) {
                case std::cv_status::no_timeout: {
                    return_code = 1;  // closeStream
                    break;
                }

                case std::cv_status::timeout: {
                    // each full queue match with its sample_time_us
                    // we don't play partial filled queue
                    // exccept for the closed queue case
                    std::memset(buffer, 0, needed * sizeof(T));
                    return 0;
                }
            }
            break;
        }

        auto& frame = res.value();
        assert(frame);
        assert(static_cast<unsigned int>(frame->ch_layout.nb_channels) == self.channels);

        T* samples = reinterpret_cast<T*>(frame->data[0]);
        std::size_t len = static_cast<std::size_t>(frame->nb_samples) *
                          static_cast<std::size_t>(frame->ch_layout.nb_channels);

        if (frame->pts != AV_NOPTS_VALUE) {
            std::int64_t pts = av_rescale_q(frame->pts, frame->time_base, AV_TIME_BASE_Q);
            std::int64_t target = self.state.current_us();

            if (pts < target - sample_time_us) {
                // too late
                continue;
            } else if (pts > target + sample_time_us) {
                // too early
                while (pts > target + sample_time_us) {
                    self.state.next_current_us(sample_time_us);
                    target = self.state.current_us();
                }
            }
        }

        self.queue<T>().append_range(std::span<T>(samples, len));
    }

    const std::size_t n = std::min(self.queue<T>().size(), needed);
    if (!self.queue<T>().empty()) std::memcpy(buffer, self.queue<T>().data(), n * sizeof(T));
    if (n < needed) std::memset(buffer + n, 0, (needed - n) * sizeof(T));
    self.queue<T>().erase(self.queue<T>().begin(), self.queue<T>().begin() + n);

    self.state.next_current_us(sample_time_us);

    if (self.state.is_seek()) {
        self.state.handle_seek();
        self.queue<T>().clear();
    }

    return return_code;
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
    if (dac.isStreamRunning()) dac.stopStream();
    if (dac.isStreamOpen()) dac.closeStream();
}

void Audio::play() {
    state.init();
    if (dac.startStream())
        throw std::runtime_error(dac.getErrorText());
}
