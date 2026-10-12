#include "video.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "app.hpp"
#include "ringbuf.hpp"

void Video::play() {
    while (!window.should_close()) {
        if (!render()) break;
    }
}

bool Video::render() {
    window.render(screen.data());
    auto res = receiver.receive_timeout(std::chrono::milliseconds(1));
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
