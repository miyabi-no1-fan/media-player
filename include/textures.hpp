#pragma once
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "descriptors.hpp"
#include "pipeline.hpp"

namespace aglea {

class Texture {
   protected:
    // Renderer& renderer;
    std::unique_ptr<GraphicsPipeline> pipeline;
    vk::UniquePipelineLayout pipeline_layout;
    std::unique_ptr<DescriptorSetLayout> descriptor_set_layout;
    std::vector<vk::UniqueDescriptorSet> descriptor_sets;

   public:
    Texture() {}
    virtual ~Texture() = default;
    virtual void init() = 0;
    virtual void draw(const vk::UniqueCommandBuffer& cmd, std::uint32_t i) = 0;

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) = default;
    Texture& operator=(Texture&&) = default;
};

}  // namespace aglea
