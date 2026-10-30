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
#include "video.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
}

#include "matrix.hpp"
#include "state.hpp"
#include "textures.hpp"
#include "textures/progress_bar.hpp"
#include "textures/video_texture.hpp"

void Video::play() {
    GLFWwindow* w = window.get_window();

    bool key_space_locked = false;
    bool click_left_locked = false;

    std::int64_t key_cooldown_us = 0;
    constexpr std::int64_t COOLDOWN = AV_TIME_BASE / 10;  // 0.1s

    while (render()) {
        if (window.should_close()) std::quick_exit(0);

        {
            int key_space = glfwGetKey(w, GLFW_KEY_SPACE);

            if (key_space == GLFW_PRESS) {
                if (!key_space_locked) pause();
                key_space_locked = true;
            }

            if (key_space == GLFW_RELEASE) {
                key_space_locked = false;
            }
        }

        {
            int click_left = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT);

            if (click_left == GLFW_PRESS) {
                if (!click_left_locked) pause();
                click_left_locked = true;
            }

            if (click_left == GLFW_RELEASE) {
                click_left_locked = false;
            }
        }

        if (key_cooldown_us == 0 && seek_req_ok.load() && seek_available.load()) {
            int key_right = glfwGetKey(w, GLFW_KEY_RIGHT);

            if (key_right == GLFW_PRESS) {
                auto target_us = std::clamp(
                    global_current_us() + 10 * AV_TIME_BASE,
                    std::int64_t(0),
                    duration_us);
                seek(target_us);
                show_progress_bar();
                key_cooldown_us = COOLDOWN;
            }
        }

        if (key_cooldown_us == 0 && seek_req_ok.load() && seek_available.load()) {
            int key_left = glfwGetKey(w, GLFW_KEY_LEFT);

            if (key_left == GLFW_PRESS) {
                auto target_us = std::clamp(
                    global_current_us() - 10 * AV_TIME_BASE,
                    std::int64_t(0),
                    duration_us);
                seek(target_us);
                show_progress_bar();
                key_cooldown_us = COOLDOWN;
            }
        }

        {
            float diff = get_cursor_absdiff();
            if (diff <= std::numeric_limits<float>::epsilon() * 2) {
                static_cursor_us += frame_time_us;
                if (static_cursor_us >= 3 * AV_TIME_BASE) {
                    hide_cursor();
                    hide_progress_bar();
                }
            } else {
                show_cursor();
                show_progress_bar();
            }
        }

        key_cooldown_us = std::max(key_cooldown_us - frame_time_us, std::int64_t(0));
    }
}

void Video::pause() {
    if (paused.load()) {
        paused.store(false);
    } else {
        paused.store(true);
        show_progress_bar();
    }
}

void Video::seek(std::int64_t target_us) {
    paused.store(false);
    seek_time_us.store(target_us);
    audio_current_us.store(nullptr);
    decoder_seek_req.store(true);
    audio_seek_req.store(true);
    audio_seek_req.wait(true);
    seek_req_ok.wait(true);
}

bool Video::render() {
    window.poll_events();

    Decoder::Frame decoded = {};
    if (!paused.load() && has_video) {
        while (1) {
            auto res = Frame::Video::receiver->receive();
            if (!res) {
                // edge case: quit while seek_req_ok is false
                seek_req_ok.store(true);
                seek_req_ok.notify_all();
                // !res means end of input
                return false;
            }

            std::int64_t pts = av_rescale_q(res->frame->best_effort_timestamp, res->frame->time_base, AV_TIME_BASE_Q);
            if (!pts_offset) pts_offset = pts;
            else pts -= *pts_offset;

            std::int64_t target = global_current_us();

            if (pts < target - frame_time_us) {
                // underrun
                continue;
            } else if (pts > target + frame_time_us) {
                // overrun
                while (pts > target + frame_time_us) {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    target = global_current_us();
                }
                decoded = std::move(*res);
                break;
            } else {
                decoded = std::move(*res);
                break;
            }
        }
    }

    // video frame is ready, so we clear this flag
    if (!seek_req_ok.load()) {
        seek_req_ok.store(true);
        seek_req_ok.notify_all();
    }

    if (!has_video) {
        // We skipped receive because of no video,
        // so we won't receive any EndOfInput signal from decoder.
        // This is the fallback
        if (global_current_us() >= duration_us) return false;
    }

    float progress =
        (should_hide_progress_bar)
            ? 0.f
            : static_cast<float>(global_current_us()) / static_cast<float>(duration_us);

    if (decoded.frame == nullptr &&
        !window.was_window_resized() &&
        progress == prev_progress)
        return true;

    prev_progress = progress;

    if (auto i = renderer.acquire_next_frame()) {
        if (tx_video) tx_video->update(decoded.frame, transformation, *i);
        if (tx_progress) tx_progress->update(progress, *i);
        renderer.record_command_buffer(*i);
        renderer.submit_frame(*i);
    }

    return true;
}

Video::Video(std::uint32_t width, std::uint32_t height, double fps, std::int64_t duration_us, const std::string& name, aglea::mat2x2 transformation)
    : width(static_cast<std::size_t>(width)),
      height(static_cast<std::size_t>(height)),
      frame_time_us(static_cast<std::int64_t>(AV_TIME_BASE / fps)),
      duration_us(duration_us),
      transformation(transformation),
      window(width, height, name, 1.0 / fps),
      renderer(window) {
    #ifndef NDEBUG
    std::cout << "Video display: " << width << "x" << height << "@" << fps << std::endl;
    #endif
    has_video = true;
    tx_video = std::make_unique<aglea::VideoTexture>(renderer, width, height);
    tx_progress = std::make_unique<aglea::ProgressBar>(renderer);
    tx_video->init();
    tx_progress->init();
    renderer.textures.push_back(static_cast<aglea::Texture*>(tx_video.get()));
    renderer.textures.push_back(static_cast<aglea::Texture*>(tx_progress.get()));
    renderer.invalidate_command_buffers();
}

Video::Video(std::int64_t duration_us, const std::string& name)
    : duration_us(duration_us),
      window(1, 1, name, 1.0 / 60),
      renderer(window) {
    tx_progress = std::make_unique<aglea::ProgressBar>(renderer);
    tx_progress->init();
    renderer.textures.push_back(static_cast<aglea::Texture*>(tx_progress.get()));
    renderer.invalidate_command_buffers();
}

Video::~Video() {
    renderer.ctx.device->waitIdle();
}

void Video::get_cursor() {
    double x, y;
    glfwGetCursorPos(window.get_window(), &x, &y);
    cursor_x = static_cast<float>(x);
    cursor_y = static_cast<float>(y);
}

void Video::hide_cursor() {
    glfwSetInputMode(window.get_window(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void Video::show_cursor() {
    glfwSetInputMode(window.get_window(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

float Video::get_cursor_absdiff() {
    float prev_x = cursor_x;
    float prev_y = cursor_y;
    get_cursor();
    float diff_x = prev_x - cursor_x;
    float diff_y = prev_y - cursor_y;
    return diff_x * diff_x + diff_y * diff_y;
}

void Video::show_progress_bar() {
    static_cursor_us = 0;
    should_hide_progress_bar = false;
}

void Video::hide_progress_bar() {
    static_cursor_us = 0;
    should_hide_progress_bar = true;
}
