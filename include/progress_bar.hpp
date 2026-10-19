#pragma once
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "buffer.hpp"
#include "descriptors.hpp"
#include "pipeline.hpp"
#include "swap_chain.hpp"

namespace aglea {

class ProgressBar {
    std::unique_ptr<GraphicsPipeline> pipeline;
    vk::UniquePipelineLayout pipeline_layout;

    std::unique_ptr<DescriptorSetLayout> descriptor_set_layout;
    std::vector<vk::UniqueDescriptorSet> descriptor_sets;

    struct UBO {
        // current / duration
        float progress;
    };

    std::vector<std::unique_ptr<Buffer>> ubo;

   public:
    ProgressBar() {}

    ProgressBar(Context& ctx, DescriptorPool& descriptor_pool, const SwapChain& swap_chain) {
        ubo.resize(swap_chain.image_count());
        for (auto&& v : ubo) {
            v = std::make_unique<Buffer>(
                ctx,
                sizeof(UBO),
                1,
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            v->map();
            auto* m = static_cast<UBO*>(v->get_mapped_memory());
            m->progress = 0.f;
        }

        descriptor_set_layout =
            DescriptorSetLayout::Builder(ctx)
                .add_binding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
                .build();

        descriptor_sets.resize(swap_chain.image_count());
        for (std::size_t i = 0; i < swap_chain.image_count(); i++) {
            descriptor_sets[i] =
                DescriptorWriter(ctx, *descriptor_set_layout, descriptor_pool)
                    .write_buffer(0, ubo[i]->descriptor_info())
                    .build();
        }

        pipeline_layout =
            ctx.device->createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo(
                vk::PipelineLayoutCreateFlags(),
                *descriptor_set_layout->get_descriptor_set_layout()));

        auto pipeline_config = GraphicsPipeline::default_config();
        pipeline_config.input_assembly.setTopology(vk::PrimitiveTopology::eTriangleStrip);

        pipeline = std::make_unique<GraphicsPipeline>(
            ctx,
            pipeline_layout,
            swap_chain.get_render_pass(),
            0,
            "shaders/dist/progress_bar.vert.spv",
            "shaders/dist/progress_bar.frag.spv",
            pipeline_config);
    }

    void update(float progress, std::uint32_t i) {
        static_cast<UBO*>(ubo[i]->get_mapped_memory())->progress = progress;
    }

    void draw(const vk::UniqueCommandBuffer& cmd, std::uint32_t i) {
        pipeline->bind(cmd);
        cmd->bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            *pipeline_layout,
            0,
            *descriptor_sets[i],
            {});
        cmd->draw(4, 1, 0, 0);
    }
};

}  // namespace aglea