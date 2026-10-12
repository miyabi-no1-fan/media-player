#include "app.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan.hpp>

#include "buffer.hpp"
#include "descriptors.hpp"
#include "image.hpp"
#include "matrix.hpp"
#include "pipeline.hpp"
#include "window.hpp"

namespace aglea {

App::~App() {
    ctx.device->waitIdle();
}

App::App(std::uint32_t width, std::uint32_t height, double frame_time, const std::string& name)
    : width(width),
      height(height),
      window(width, height, name, frame_time),
      ctx(window) {
    swap_chain = std::make_unique<SwapChain>(ctx, window.get_extent());

    descriptor_pool =
        DescriptorPool::Builder(ctx)
            .set_max_sets(swap_chain->image_count())
            .add_pool_size(vk::DescriptorType::eUniformBuffer, swap_chain->image_count())
            .add_pool_size(vk::DescriptorType::eCombinedImageSampler, swap_chain->image_count())
            .build();

    global_ubo.resize(swap_chain->image_count());
    for (auto&& v : global_ubo) {
        v = std::make_unique<Buffer>(
            ctx,
            sizeof(GlobalUBO),
            1,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        v->map();
    }

    image_buffer.resize(swap_chain->image_count());
    ctx.single_time_commands([this](const vk::UniqueCommandBuffer& cmd) {
        for (std::size_t i = 0; i < swap_chain->image_count(); i++) {
            image_buffer[i] = std::make_unique<Image>(
                ctx,
                vk::ImageType::e2D,
                vk::Format::eR8G8B8Unorm,
                vk::Extent3D(this->width, this->height, 1),
                vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            image_buffer[i]->transition_image_layout(
                cmd,
                vk::ImageLayout::eGeneral,
                {},
                {},
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eAllCommands);
        }
    });

    image_sampler = ctx.device->createSamplerUnique(vk::SamplerCreateInfo(
        {},
        vk::Filter::eLinear,
        vk::Filter::eLinear,
        vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eClampToBorder,
        vk::SamplerAddressMode::eClampToBorder,
        vk::SamplerAddressMode::eClampToBorder,
        0,
        vk::False,
        0,
        vk::False,
        vk::CompareOp::eNever,
        0,
        0,
        vk::BorderColor::eFloatOpaqueBlack,
        vk::False));

    staged_image = std::make_unique<Buffer>(
        ctx,
        3,
        this->width * this->height,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    staged_image->map();

    descriptor_set_layout =
        DescriptorSetLayout::Builder(ctx)
            .add_binding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
            .add_binding(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &*image_sampler)
            .build();

    descriptor_sets.resize(swap_chain->image_count());
    for (std::size_t i = 0; i < swap_chain->image_count(); i++) {
        descriptor_sets[i] =
            DescriptorWriter(ctx, *descriptor_set_layout, *descriptor_pool)
                .write_buffer(0, global_ubo[i]->descriptor_info())
                .write_image(1, image_buffer[i]->descriptor_info())
                .build();
    }

    graphics_pipeline_layout =
        ctx.device->createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo(
            vk::PipelineLayoutCreateFlags(),
            *descriptor_set_layout->get_descriptor_set_layout()));

    auto graphics_pipeline_config = GraphicsPipeline::default_config();
    graphics_pipeline_config.input_assembly.setTopology(vk::PrimitiveTopology::eTriangleStrip);

    graphics_pipeline = std::make_unique<GraphicsPipeline>(
        ctx,
        graphics_pipeline_layout,
        swap_chain->get_render_pass(),
        0,
        "shaders/dist/shader.vert.spv",
        "shaders/dist/shader.frag.spv",
        graphics_pipeline_config);

    command_buffers.resize(swap_chain->image_count());
    auto cmd = ctx.create_command_buffers(command_buffers.size());
    for (std::size_t i = 0; i < command_buffers.size(); i++) {
        command_buffers[i].first = false;
        command_buffers[i].second = std::move(cmd[i]);
    }
}

void App::create_swap_chain() {
    auto extent = window.get_extent();
    while (extent.width == 0 || extent.height == 0) {
        extent = window.get_extent();
        window.wait_events();
    }
    ctx.device->waitIdle();

    std::unique_ptr<SwapChain> old_swap_chain = std::move(swap_chain);
    swap_chain = std::make_unique<SwapChain>(ctx, extent, old_swap_chain.get());
    if (
        old_swap_chain &&
        (!old_swap_chain->cmpeq_swap_formats(*swap_chain) ||
         old_swap_chain->image_count() != swap_chain->image_count())) {
        throw std::runtime_error("Swap chain has changed unexpectedly");
    }
    old_swap_chain = nullptr;
}

void App::render(const void* data, mat2x2 transformation) {
    window.poll_events();
    if (auto i = acquire_next_frame()) {
        update_global_ubo(*i, transformation);
        std::memcpy(staged_image->get_mapped_memory(), data, width * height * 3);
        record_command_buffer(*i);
        submit_frame(*i);
    }
}

void App::update_global_ubo(std::size_t i, mat2x2 A) {
    float w = static_cast<float>(width);
    float h = static_cast<float>(height);
    float W = static_cast<float>(swap_chain->width());
    float H = static_cast<float>(swap_chain->height());

    float half_width = (w - 1) / 2;
    float half_height = (h - 1) / 2;

    // 1    2
    // 3    4
    const float x1 = -half_width * A[0][0] + half_height * A[0][1];
    const float x2 = half_width * A[0][0] + half_height * A[0][1];
    const float x3 = -half_width * A[0][0] - half_height * A[0][1];
    const float x4 = half_width * A[0][0] - half_height * A[0][1];

    const float y1 = -half_width * A[1][0] + half_height * A[1][1];
    const float y2 = half_width * A[1][0] + half_height * A[1][1];
    const float y3 = -half_width * A[1][0] - half_height * A[1][1];
    const float y4 = half_width * A[1][0] - half_height * A[1][1];

    const float xmax = std::max(std::max(std::max(x1, x2), x3), x4);
    const float xmin = std::min(std::min(std::min(x1, x2), x3), x4);
    const float ymax = std::max(std::max(std::max(y1, y2), y3), y4);
    const float ymin = std::min(std::min(std::min(y1, y2), y3), y4);

    const uint32_t box_width = (uint32_t)(xmax - xmin + 1);
    const uint32_t box_height = (uint32_t)(ymax - ymin + 1);

    float scale = std::min(W / box_width, H / box_height);

    // [1, 1] -> [w/2, h/2]
    mat2x2 unit = mat2x2::zeros();
    unit[0][0] = w * 0.5f;
    unit[1][1] = h * 0.5f;

    // [W/2, H/2] -> [1, 1]
    mat2x2 un_unit = mat2x2::zeros();
    un_unit[0][0] = 2.0f / W;
    un_unit[1][1] = 2.0f / H;

    auto* v = static_cast<GlobalUBO*>(global_ubo[i]->get_mapped_memory());
    v->transformation = scale * (un_unit * A * unit);
}

std::optional<std::uint32_t> App::acquire_next_frame() {
    std::uint32_t i;
    vk::Result res = swap_chain->acquire_next_image(i);
    if (res == vk::Result::eErrorOutOfDateKHR) {
        create_swap_chain();
        for (auto&& p : command_buffers) p.first = false;
        return {};
    }
    if (res != vk::Result::eSuccess && res != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("Can't acquire swap chain image. Vulkan Error code: " + std::to_string((int)res));
    }
    return i;
}

void App::submit_frame(std::uint32_t i) {
    auto res = swap_chain->submit_command_buffer(command_buffers[i].second, i);
    if (res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR || window.was_window_resized()) {
        window.reset_window_resized_flag();
        create_swap_chain();
        for (auto&& p : command_buffers) p.first = false;
    } else if (res != vk::Result::eSuccess) {
        throw std::runtime_error("Can't present swap chain image. Vulkan Error code: " + std::to_string((int)res));
    }
}

void App::record_command_buffer(std::uint32_t i) {
    if (!command_buffers[i].first) {
        auto& cmd = command_buffers[i].second;
        cmd->begin(vk::CommandBufferBeginInfo());

        {
            vk::MemoryBarrier before({}, vk::AccessFlagBits::eTransferWrite);
            cmd->pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer, {}, before, {}, {});

            // clang-format off
            vk::BufferImageCopy region(
                0, 0, 0,
                vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
                { 0, 0, 0 },
                image_buffer[i]->get_extent()
            );
            // clang-format on
            cmd->copyBufferToImage(*staged_image->get_buffer(), *image_buffer[i]->get_image(), vk::ImageLayout::eGeneral, region);

            vk::MemoryBarrier after(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead);
            cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, after, {}, {});
        }

        vk::ClearValue clear_values[2];
        clear_values[0].color = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f);
        clear_values[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);
        cmd->beginRenderPass(
            vk::RenderPassBeginInfo(
                *swap_chain->get_render_pass(),
                *swap_chain->get_frame_buffer(i),
                vk::Rect2D({ 0, 0 }, swap_chain->get_swap_chain_extent()),
                clear_values),
            vk::SubpassContents::eInline);
        auto extent = swap_chain->get_swap_chain_extent();
        vk::Viewport viewport(
            0.0f,
            0.0f,
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
            0.0f,
            1.0f);
        vk::Rect2D scissor = { { 0, 0 }, extent };
        cmd->setViewport(0, viewport);
        cmd->setScissor(0, scissor);

        graphics_pipeline->bind(cmd);
        cmd->bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            *graphics_pipeline_layout,
            0,
            *descriptor_sets[i],
            {});
        cmd->draw(4, 1, 0, 0);

        cmd->endRenderPass();
        cmd->end();
        command_buffers[i].first = true;
    }
}

}  // namespace aglea
