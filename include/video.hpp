#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "app.hpp"
#include "decoder.hpp"
#include "ringbuf.hpp"

class Video {
    std::size_t width;
    std::size_t height;
    std::size_t bpp = 4;
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

    void play() {
        while (!window.should_close()) {
            window.render(screen.data());
            auto res = receiver.receive_timeout(std::chrono::milliseconds(1));
            if (!res && res.error() == RingBuf::Error::EndOfInput) break;
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
        }
    }
};
