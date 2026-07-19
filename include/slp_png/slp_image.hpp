#include "slp_png.h"
#include "utils.hpp"

class Image {
    slp_image_t image = {};

   public:
    uint32_t height = 0;
    uint32_t width = 0;
    uint32_t channels = 0;
    uint8_t bit_depth = 0;
    size_t image_size = 0;

    Image(const char* path) {
        this->image = slp_png_read(path);
        if (this->image.buffer == NULL) {
            panic("Read image Failed");
        }

        this->height = this->image.height;
        this->width = this->image.width;
        this->channels = this->image.channels;
        this->bit_depth = this->image.bit_depth;
        this->image_size = this->image.image_size;
    }

    ~Image() {
        slp_image_destroy(&this->image);
    }

    void* data() const {
        return this->image.buffer;
    }
};
