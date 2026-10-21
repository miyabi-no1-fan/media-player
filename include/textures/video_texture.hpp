#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>
extern "C" {
#include <libavutil/frame.h>
}

#include "buffer.hpp"
#include "image.hpp"
#include "matrix.hpp"
#include "renderer.hpp"
#include "textures.hpp"

namespace aglea {

class VideoTexture : public Texture {
   private:
    Renderer& renderer;
    std::uint32_t width;
    std::uint32_t height;

    struct UBO {
        mat2x2 transformation = mat2x2::identity();
        std::uint32_t is_color_range_jpeg;
        std::uint32_t is_color_space_bt709;
    };

    std::vector<std::unique_ptr<Buffer>> ubo;

    vk::UniqueSampler image_sampler;
    std::vector<std::array<std::unique_ptr<Image>, 3>> image_planes;
    std::vector<std::array<std::uint8_t*, 3>> mapped_images;
    std::vector<std::array<vk::SubresourceLayout, 3>> mapped_image_layouts;

    std::uint32_t prev_image_index = std::numeric_limits<std::uint32_t>::max();

    bool is_image_transfer_supported = false;
    bool is_image_linear_filtering_supported = false;

   public:
    VideoTexture(Renderer& renderer,
                 std::uint32_t width,
                 std::uint32_t height)
        : renderer(renderer),
          width(width),
          height(height) {}
    void init() override;
    void update(AVFrame* frame, const mat2x2& transform, std::uint32_t i);
    void draw(const vk::UniqueCommandBuffer& cmd, std::uint32_t i) override;

   private:
    void check_supported_features();
    void create_ubo();
    void create_image_resources();
    void create_descriptors();
    void create_pipeline();
    void update_ubo(const mat2x2& A, std::size_t i);
    void update_image(AVFrame* frame, std::uint32_t i);
};

}  // namespace aglea