#include "app.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan.hpp>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include "buffer.hpp"
#include "descriptors.hpp"
#include "image.hpp"
#include "matrix.hpp"
#include "pipeline.hpp"

template <typename A, typename B = A, typename C = A>
static inline C div_ceil(const A& a, const B& b) {
    return (a / b) + (a % b != 0);
}

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
            .add_pool_size(vk::DescriptorType::eCombinedImageSampler, swap_chain->image_count() * 3)
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

    image_planes.resize(swap_chain->image_count());
    mapped_images.resize(swap_chain->image_count());
    mapped_image_layouts.resize(swap_chain->image_count());
    ctx.single_time_commands([this](const vk::UniqueCommandBuffer& cmd) {
        for (std::size_t i = 0; i < swap_chain->image_count(); i++) {
            for (std::size_t p = 0; p < 3; p++) {
                std::uint32_t width = this->width;
                std::uint32_t height = this->height;
                if (p > 0) {
                    width = div_ceil(width, 2);
                    height = div_ceil(height, 2);
                }
                image_planes[i][p] = std::make_unique<Image>(
                    ctx,
                    vk::ImageType::e2D,
                    vk::Format::eR8Unorm,
                    vk::ImageTiling::eLinear,
                    vk::Extent3D(width, height, 1),
                    vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
                image_planes[i][p]->transition_image_layout(
                    cmd,
                    vk::ImageLayout::eGeneral,
                    {},
                    {},
                    vk::PipelineStageFlagBits::eTopOfPipe,
                    vk::PipelineStageFlagBits::eAllCommands);
                image_planes[i][p]->map();
                mapped_image_layouts[i][p] = ctx.device->getImageSubresourceLayout(
                    *image_planes[i][p]->get_image(),
                    vk::ImageSubresource{ vk::ImageAspectFlagBits::eColor, 0, 0 });
                mapped_images[i][p] = static_cast<std::uint8_t*>(image_planes[i][p]->get_mapped_memory());
            }
        }
    });

    image_sampler = ctx.device->createSamplerUnique(vk::SamplerCreateInfo(
        {},
        vk::Filter::eLinear,
        vk::Filter::eLinear,
        vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge,
        0,
        vk::False,
        0,
        vk::False,
        vk::CompareOp::eNever,
        0,
        0,
        vk::BorderColor::eFloatOpaqueBlack,
        vk::False));

    descriptor_set_layout =
        DescriptorSetLayout::Builder(ctx)
            .add_binding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
            .add_binding(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, &*image_sampler)
            .add_binding(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, &*image_sampler)
            .add_binding(3, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, &*image_sampler)
            .build();

    descriptor_sets.resize(swap_chain->image_count());
    for (std::size_t i = 0; i < swap_chain->image_count(); i++) {
        descriptor_sets[i] =
            DescriptorWriter(ctx, *descriptor_set_layout, *descriptor_pool)
                .write_buffer(0, global_ubo[i]->descriptor_info())
                .write_image(1, image_planes[i][0]->descriptor_info())
                .write_image(2, image_planes[i][1]->descriptor_info())
                .write_image(3, image_planes[i][2]->descriptor_info())
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

void App::render(AVFrame* frame, mat2x2 transformation) {
    window.poll_events();
    if (frame == nullptr && !window.was_window_resized()) return;
    if (auto i = acquire_next_frame()) {
        update_global_ubo(*i, transformation);
        draw(frame, *i);
        record_command_buffer(*i);
        submit_frame(*i);
        prev_image_index = *i;
    }
}

void App::update_global_ubo(std::size_t i, mat2x2 A) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float W = static_cast<float>(swap_chain->width());
    const float H = static_cast<float>(swap_chain->height());

    const float half_width = (w - 1) / 2;
    const float half_height = (h - 1) / 2;

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

    const uint32_t box_width = static_cast<uint32_t>(xmax - xmin + 1);
    const uint32_t box_height = static_cast<uint32_t>(ymax - ymin + 1);

    float scale_to_fit = std::min(W / box_width, H / box_height);

    // [1, 1] -> [w/2, h/2]
    mat2x2 to_pixels_coord = mat2x2::zeros();
    to_pixels_coord[0][0] = w / 2.0f;
    to_pixels_coord[1][1] = h / 2.0f;

    // [W/2, H/2] -> [1, 1]
    mat2x2 norm = mat2x2::zeros();
    norm[0][0] = 2.0f / W;
    norm[1][1] = 2.0f / H;

    auto* v = static_cast<GlobalUBO*>(global_ubo[i]->get_mapped_memory());
    v->transformation = norm * scale_to_fit * A * to_pixels_coord;
}

void App::draw(AVFrame* frame, std::uint32_t i) {
    if (frame == nullptr) {
        if (prev_image_index == std::numeric_limits<std::uint32_t>::max()) return;
        if (prev_image_index == i) return;
        ctx.single_time_commands([this, i](vk::UniqueCommandBuffer& cmd) {
            for (std::size_t p = 0; p < 3; p++) {
                const auto& src = image_planes[prev_image_index][p];
                const auto& dst = image_planes[i][p];
                cmd->copyImage(
                    *src->get_image(),
                    src->get_layout(),
                    *dst->get_image(),
                    dst->get_layout(),
                    vk::ImageCopy(
                        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
                        vk::Offset3D(0, 0, 0),
                        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
                        vk::Offset3D(0, 0, 0),
                        std::min(dst->get_extent(), src->get_extent())));
            }
        });
        return;
    }
    for (std::size_t p = 0; p < 3; p++) {
        std::uint8_t* dst = mapped_images[i][p] + mapped_image_layouts[i][p].offset;
        std::size_t dst_pitch = mapped_image_layouts[i][p].rowPitch;

        std::uint8_t* src = frame->data[p];
        std::size_t src_pitch = frame->linesize[p];

        auto extent = image_planes[i][p]->get_extent();
        std::size_t height = std::min(
            static_cast<std::size_t>(extent.height),
            static_cast<std::size_t>(frame->height));
        std::size_t width = std::min(
            static_cast<std::size_t>(extent.width),
            static_cast<std::size_t>(frame->width));

        for (std::size_t i = 0; i < height; i++) {
            std::memcpy(dst + i * dst_pitch,
                        src + i * src_pitch,
                        width);
        }
    }
    auto* v = static_cast<GlobalUBO*>(global_ubo[i]->get_mapped_memory());
    v->is_color_range_jpeg = frame->color_range == AVCOL_RANGE_JPEG;
    v->is_color_space_bt709 = frame->colorspace == AVCOL_SPC_BT709;
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

        vk::ClearValue clear_values[2];
        clear_values[0].color = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f);
        // clear_values[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);
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
            static_cast<float>(extent.height));
        //  0.0f,
        //  1.0f);
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
