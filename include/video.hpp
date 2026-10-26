#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
extern "C" {
#include <libavutil/avutil.h>
}

#include "matrix.hpp"
#include "renderer.hpp"
#include "textures/progress_bar.hpp"
#include "textures/video_texture.hpp"
#include "window.hpp"

class Video {
    std::size_t width = 1;
    std::size_t height = 1;
    std::size_t bpp = 3;
    std::int64_t frame_time_us = static_cast<std::int64_t>(AV_TIME_BASE / 60.f);
    std::int64_t duration_us;
    std::optional<std::int64_t> pts_offset = std::nullopt;
    aglea::mat2x2 transformation = aglea::mat2x2::identity();
    float cursor_x;
    float cursor_y;
    std::int64_t static_cursor_us = 0;
    bool should_hide_progress_bar = false;
    float prev_progress = 0.f;
    bool has_video = false;

    aglea::Window window;
    aglea::Renderer renderer;
    std::unique_ptr<aglea::VideoTexture> tx_video = nullptr;
    std::unique_ptr<aglea::ProgressBar> tx_progress = nullptr;

   public:
    Video(std::uint32_t width,
          std::uint32_t height,
          double fps,
          std::int64_t duration_us,
          const std::string& name,
          aglea::mat2x2 transformation = aglea::mat2x2::identity());
    Video(std::int64_t duration_us, const std::string& name);
    ~Video();

    Video(const Video&) = delete;
    Video& operator=(const Video&) = delete;
    Video(Video&&) = delete;
    Video& operator=(Video&&) = delete;

    std::size_t get_width() const { return width; }
    std::size_t get_height() const { return height; }
    std::size_t row_stride() const { return width * bpp; }
    std::size_t size() const { return height * row_stride(); }

    void play();

   private:
    bool render();
    void pause();
    void seek(std::int64_t target_us);
    void get_cursor();
    void hide_cursor();
    void show_cursor();
    void show_progress_bar();
    void hide_progress_bar();
    float get_cursor_absdiff();
};
