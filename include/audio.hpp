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

#include <atomic>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include "decoder.hpp"
#include "ringbuf.hpp"

class Audio {
    RtAudio dac;
    unsigned int channels;
    unsigned int sample_rate;
    std::shared_ptr<RingBuf::Receiver<Decoder::Frame>> receiver = nullptr;
    std::shared_ptr<std::atomic_int64_t> current_us = nullptr;
    std::optional<std::atomic_int64_t> pts_offset = 0;

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
    Audio(unsigned int channels, unsigned int sample_rate, RtAudioFormat sample_format);
    void play();
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    Audio(Audio&&) noexcept = delete;
    Audio& operator=(Audio&&) noexcept = delete;

   private:
    template <typename T>
    static int callback(void* output_buffer, void* input_buffer, unsigned int nbuffer_frames,
                        double stream_time, RtAudioStreamStatus status, void* user_data);
    void new_state();
};
