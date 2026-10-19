#include "video.hpp"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
}
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <thread>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "matrix.hpp"
#include "ringbuf.hpp"
#include "state.hpp"

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
    std::int64_t static_cursor_us = 0;

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
                }
                key_left_locked = true;
            }

            if (key_left == GLFW_RELEASE) {
                key_left_locked = false;
            }
        }

        {
            float diff = get_cursor_absdiff();
            if (diff <= std::numeric_limits<float>::epsilon()) {
                static_cursor_us += frame_time_us;
                if (static_cursor_us >= 2 * AV_TIME_BASE) {
                    hide_cursor();
                    static_cursor_us = 0;
                    should_hide_progress_bar = true;
                }
            } else {
                show_cursor();
                static_cursor_us = 0;
                should_hide_progress_bar = false;
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
    if (paused.load()) {
        window.render(nullptr, progress(), transformation);
        return true;
    }
    auto res = Frame::Video::receiver.receive_timeout(std::chrono::microseconds(frame_time_us) * 0.8);
    if (!res && res.error() == RingBuf::Error::EndOfInput) return false;
    if (res) window.render(res->frame, progress(), transformation);
    else window.render(nullptr, progress(), transformation);
    return true;
}

Video::Video(std::uint32_t width, std::uint32_t height, double fps, std::int64_t duration_us, const std::string& name, aglea::mat2x2 transformation)
    : width(static_cast<std::size_t>(width)),
      height(static_cast<std::size_t>(height)),
      frame_time_us(static_cast<std::int64_t>(AV_TIME_BASE / fps)),
      duration_us(duration_us),
      window(width, height, 1.0 / fps, name),
      transformation(transformation) {}

float Video::progress() const {
    if (should_hide_progress_bar) return 0.f;
    return static_cast<float>(current_us) /
           static_cast<float>(duration_us);
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
    return std::sqrt(diff_x * diff_x + diff_y * diff_y);
}
