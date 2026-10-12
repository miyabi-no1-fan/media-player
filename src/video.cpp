#include "video.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "app.hpp"
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

    while (render()) {
        if (window.should_close()) std::quick_exit(0);

        switch (glfwGetKey(w, GLFW_KEY_SPACE)) {
            case GLFW_PRESS:
                if (!key_space_locked)
                    paused.store(!paused.load());
                key_space_locked = true;
                break;
            case GLFW_RELEASE:
                key_space_locked = false;
                break;
        }

        switch (glfwGetKey(w, GLFW_KEY_RIGHT)) {
            case GLFW_PRESS:
                if (!key_right_locked) {
                    if (seek_available.load()) {
                        std::int64_t target_us =
                            std::clamp(frame_to_us(current_frame) + 3'000'000,
                                       std::int64_t(0),
                                       duration_us);
                        seek(target_us);
                        current_frame = us_to_frame(target_us);
                    }
                }
                key_right_locked = true;
                break;
            case GLFW_RELEASE:
                key_right_locked = false;
                break;
        }

        switch (glfwGetKey(w, GLFW_KEY_LEFT)) {
            case GLFW_PRESS:
                if (!key_left_locked) {
                    if (seek_available.load()) {
                        std::int64_t target_us =
                            std::clamp(frame_to_us(current_frame) - 3'000'000,
                                       std::int64_t(0),
                                       duration_us);
                        seek(target_us);
                        current_frame = us_to_frame(target_us);
                    }
                }
                key_left_locked = true;
                break;
            case GLFW_RELEASE:
                key_left_locked = false;
                break;
        }

        current_frame++;
    }
}

void Video::seek(std::int64_t target_us) {
    seek_time_us.store(target_us);
    seek_req.store(true);
    video_idle.store(true);
    while (seek_req.load()) std::this_thread::sleep_for(std::chrono::microseconds(100));
    video_idle.store(false);
}

bool Video::render() {
    window.render(screen.data());
    if (paused.load()) return true;
    const std::chrono::duration<double> time_budget(1.0 / fps);
    auto res = Frame::Video::receiver.receive_timeout(time_budget * 0.8);
    if (!res && res.error() == RingBuf::Error::EndOfInput) return false;
    if (res) {
        auto* frame = res->frame;
        std::size_t src_row_stride = static_cast<std::size_t>(frame->linesize[0]);
        std::uint8_t* src_data = static_cast<std::uint8_t*>(frame->data[0]);
        std::size_t src_width = static_cast<std::size_t>(frame->width);
        std::size_t src_height = static_cast<std::size_t>(frame->height);
        for (std::size_t i = 0; i < height; i++) {
            std::memcpy(screen.data() + i * row_stride(),
                        src_data + i * src_row_stride,
                        row_stride());
        }
    }
    return true;
}

Video::Video(std::uint32_t width, std::uint32_t height, double fps, std::int64_t duration_us, const std::string& name)
    : width(static_cast<std::size_t>(width)),
      height(static_cast<std::size_t>(height)),
      fps(fps),
      duration_us(duration_us),
      window(width, height, 1.0 / fps, name),
      screen(size(), 0) {}

void Video::clear_state() {
    current_frame = 0;
    screen.assign(size(), 0);
}
