#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "buffer.hpp"
#include "renderer.hpp"
#include "textures.hpp"

namespace aglea {

class ProgressBar : public Texture {
    Renderer& renderer;
    struct UBO {
        // current / duration
        float progress;
    };
    std::vector<std::unique_ptr<Buffer>> ubo;

   public:
    ProgressBar(Renderer& renderer) : renderer(renderer) {}
    void init() override;
    void update(float progress, std::uint32_t i);
    void draw(const vk::UniqueCommandBuffer& cmd, std::uint32_t i) override;

   private:
    void create_ubo();
    void create_descriptors();
    void create_pipeline();
};

}  // namespace aglea