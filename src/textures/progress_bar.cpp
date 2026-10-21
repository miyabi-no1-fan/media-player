#include "textures/progress_bar.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vulkan/vulkan.hpp>

#include "buffer.hpp"
#include "descriptors.hpp"
#include "pipeline.hpp"
#include "renderer.hpp"

namespace aglea {

void ProgressBar::init() {
    create_ubo();
    create_descriptors();
    create_pipeline();
}

void ProgressBar::create_ubo() {
    ubo.resize(renderer.swap_chain->image_count());
    for (auto&& v : ubo) {
        v = std::make_unique<Buffer>(
            renderer.ctx,
            sizeof(UBO),
            1,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        v->map();
        auto* m = static_cast<UBO*>(v->get_mapped_memory());
        m->progress = 0.f;
    }
}

void ProgressBar::create_descriptors() {
    descriptor_set_layout =
        DescriptorSetLayout::Builder(renderer.ctx)
            .add_binding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
            .build();

    descriptor_sets.resize(renderer.swap_chain->image_count());
    for (std::size_t i = 0; i < renderer.swap_chain->image_count(); i++) {
        descriptor_sets[i] =
            DescriptorWriter(renderer.ctx, *descriptor_set_layout, *renderer.descriptor_pool)
                .write_buffer(0, ubo[i]->descriptor_info())
                .build();
    }
}

void ProgressBar::create_pipeline() {
    pipeline_layout =
        renderer.ctx.device->createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo(
            vk::PipelineLayoutCreateFlags(),
            *descriptor_set_layout->get_descriptor_set_layout()));

    auto pipeline_config = GraphicsPipeline::default_config();
    pipeline_config.input_assembly.setTopology(vk::PrimitiveTopology::eTriangleStrip);

    pipeline = std::make_unique<GraphicsPipeline>(
        renderer.ctx,
        pipeline_layout,
        renderer.swap_chain->get_render_pass(),
        0,
        "shaders/dist/progress_bar.vert.spv",
        "shaders/dist/progress_bar.frag.spv",
        pipeline_config);
}

void ProgressBar::update(float progress, std::uint32_t i) {
    static_cast<UBO*>(ubo[i]->get_mapped_memory())->progress = progress;
}

void ProgressBar::draw(const vk::UniqueCommandBuffer& cmd, std::uint32_t i) {
    pipeline->bind(cmd);
    cmd->bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *pipeline_layout,
        0,
        *descriptor_sets[i],
        {});
    cmd->draw(4, 1, 0, 0);
}

}  // namespace aglea
