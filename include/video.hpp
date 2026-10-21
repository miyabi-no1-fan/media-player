#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "matrix.hpp"
#include "renderer.hpp"
#include "textures/progress_bar.hpp"
#include "textures/video_texture.hpp"
#include "window.hpp"

class Video {
    std::size_t width;
    std::size_t height;
    std::size_t bpp = 3;
    std::int64_t frame_time_us;
    std::int64_t duration_us;
    std::int64_t current_us = 0;
    aglea::mat2x2 transformation = aglea::mat2x2::identity();
    float cursor_x;
    float cursor_y;
    bool should_hide_progress_bar = false;
    float prev_progress = 0.f;

    aglea::Window window;
    aglea::Renderer renderer;
    std::unique_ptr<aglea::VideoTexture> tx_video;
    std::unique_ptr<aglea::ProgressBar> tx_progress;

   public:
    Video(std::uint32_t width,
          std::uint32_t height,
          double fps,
          std::int64_t duration_us,
          const std::string& name,
          aglea::mat2x2 transformation = aglea::mat2x2::identity());
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
    void seek(std::int64_t target_us);
    void get_cursor();
    void hide_cursor();
    void show_cursor();
    float get_cursor_absdiff();
};
