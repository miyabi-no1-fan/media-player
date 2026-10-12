#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "app.hpp"
#include "decoder.hpp"
#include "ringbuf.hpp"

class Video {
    std::size_t width;
    std::size_t height;
    std::size_t bpp = 3;
    double fps;

    aglea::App window;
    RingBuf::Receiver<Decoder::Frame> receiver;

    std::vector<std::uint8_t> screen;

   public:
    Video(std::uint32_t width, std::uint32_t height, double fps, const std::string& name, RingBuf::Receiver<Decoder::Frame> receiver)
        : width(static_cast<std::size_t>(width)),
          height(static_cast<std::size_t>(height)),
          fps(fps),
          window(width, height, 1.0 / fps, name),
          receiver(std::move(receiver)),
          screen(size(), 0) {}

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
};
