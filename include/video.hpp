#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "app.hpp"
#include "matrix.hpp"

class Video {
    std::size_t width;
    std::size_t height;
    std::size_t bpp = 3;
    double fps;
    std::int64_t duration_us;

    std::int64_t current_frame = 0;

    aglea::App window;
    std::vector<std::uint8_t> screen;
    aglea::mat2x2 transformation = aglea::mat2x2::identity();

   public:
    Video(std::uint32_t width,
          std::uint32_t height,
          double fps,
          std::int64_t duration_us,
          const std::string& name,
          aglea::mat2x2 transformation = aglea::mat2x2::identity());

    Video(const Video&) = delete;
    Video& operator=(const Video&) = delete;
    Video(Video&&) = delete;
    Video& operator=(Video&&) = delete;

    std::size_t get_width() const { return width; }
    std::size_t get_height() const { return height; }
    std::size_t row_stride() const { return width * bpp; }
    std::size_t size() const { return height * row_stride(); }
    std::int64_t frame_to_us(std::int64_t frame) const {
        return static_cast<std::int64_t>(static_cast<double>(frame) * (1'000'000.0 / fps));
    }
    std::int64_t us_to_frame(std::int64_t us) const {
        return static_cast<std::int64_t>((static_cast<double>(us) / 1'000'000.0) * fps);
    }

    void play();
    void clear_state();

   private:
    bool render();
    void seek(std::int64_t target_us);
};
