#include "video.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <thread>
#include <utility>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
extern "C" {
#include <libavutil/avutil.h>
}

#include "matrix.hpp"
#include "ringbuf.hpp"
#include "state.hpp"
#include "textures.hpp"
#include "textures/progress_bar.hpp"
#include "textures/video_texture.hpp"

std::atomic_bool paused = false;
extern std::atomic_bool seek_available;
extern std::atomic_bool seek_req;
extern std::atomic_int64_t seek_time_us;
std::atomic_bool video_idle = false;

void Video::play() {
    GLFWwindow* w = window.get_window();

    bool key_space_locked = false;
    bool key_right_locked = false;
    bool key_left_locked = false;
    bool click_left_locked = false;

    while (render()) {
        if (window.should_close()) std::quick_exit(0);

        {
            int key_space = glfwGetKey(w, GLFW_KEY_SPACE);

            if (key_space == GLFW_PRESS) {
                if (!key_space_locked) paused.store(!paused.load());
                key_space_locked = true;
            }

            if (key_space == GLFW_RELEASE) {
                key_space_locked = false;
            }
        }

        {
            int click_left = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT);

            if (click_left == GLFW_PRESS) {
                if (!click_left_locked) paused.store(!paused.load());
                click_left_locked = true;
            }

            if (click_left == GLFW_RELEASE) {
                click_left_locked = false;
            }
        }

        if (!key_left_locked) {
            int key_right = glfwGetKey(w, GLFW_KEY_RIGHT);

            if (key_right == GLFW_PRESS) {
                if (!key_right_locked && seek_available.load()) {
                    current_us = std::clamp(
                        current_us + 3 * AV_TIME_BASE,
                        std::int64_t(0),
                        duration_us);
                    seek(current_us);
                    show_progress_bar();
                }
                key_right_locked = true;
            }

            if (key_right == GLFW_RELEASE) {
                key_right_locked = false;
            }
        }

        if (!key_right_locked) {
            int key_left = glfwGetKey(w, GLFW_KEY_LEFT);

            if (key_left == GLFW_PRESS) {
                if (!key_left_locked && seek_available.load()) {
                    current_us = std::clamp(
                        current_us - 3 * AV_TIME_BASE,
                        std::int64_t(0),
                        duration_us);
                    seek(current_us);
                    show_progress_bar();
                }
                key_left_locked = true;
            }

            if (key_left == GLFW_RELEASE) {
                key_left_locked = false;
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

        if (!paused.load())
            current_us += frame_time_us;
    }
}

void Video::seek(std::int64_t target_us) {
    paused.store(false);
    seek_time_us.store(target_us);
    seek_req.store(true);
    video_idle.store(true);
    while (seek_req.load()) std::this_thread::sleep_for(std::chrono::microseconds(100));
    video_idle.store(false);
}

bool Video::render() {
    float progress =
        (should_hide_progress_bar)
            ? 0.f
            : static_cast<float>(current_us) / static_cast<float>(duration_us);

    Decoder::Frame decoded = {};
    if (!paused.load()) {
        auto res = Frame::Video::receiver.receive_timeout(std::chrono::microseconds(frame_time_us) * 0.8);
        if (!res && res.error() == RingBuf::Error::EndOfInput) return false;
        if (res) decoded = std::move(*res);
    }

    window.poll_events();

    if (decoded.frame == nullptr &&
        !window.was_window_resized() &&
        progress == prev_progress)
        return true;

    if (auto i = renderer.acquire_next_frame()) {
        tx_video->update(decoded.frame, transformation, *i);
        tx_progress->update(progress, *i);
        renderer.record_command_buffer(*i);
        renderer.submit_frame(*i);
    }

    prev_progress = progress;
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
    tx_video = std::make_unique<aglea::VideoTexture>(renderer, width, height);
    tx_progress = std::make_unique<aglea::ProgressBar>(renderer);
    tx_video->init();
    tx_progress->init();
    renderer.textures.push_back(static_cast<aglea::Texture*>(tx_video.get()));
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
