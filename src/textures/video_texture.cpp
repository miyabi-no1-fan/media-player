#include "textures/video_texture.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vulkan/vulkan.hpp>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include "buffer.hpp"
#include "descriptors.hpp"
#include "image.hpp"
#include "pipeline.hpp"
#include "renderer.hpp"

template <typename A, typename B = A, typename C = A>
static inline constexpr C div_ceil(const A& a, const B& b) {
    return (a / b) + (a % b != 0);
}

namespace aglea {

void VideoTexture::init() {
    check_supported_features();
    create_ubo();
    create_image_resources();
    create_descriptors();
    create_pipeline();
}

void VideoTexture::check_supported_features() {
    auto prop = renderer.ctx.physical_device->getFormatProperties(vk::Format::eR8Unorm);
    // `context.cpp` already find for device with R8Unorm that support
    // this required feature, a check here is just being defensive.
    auto required = vk::FormatFeatureFlagBits::eSampledImage;
    if ((prop.linearTilingFeatures & required) != required)
        throw std::runtime_error("Device does not support required features.");

    auto transfer = vk::FormatFeatureFlagBits::eTransferDst | vk::FormatFeatureFlagBits::eTransferSrc;
    is_image_transfer_supported = (prop.linearTilingFeatures & transfer) == transfer;

    auto filter = vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    is_image_linear_filtering_supported = (prop.linearTilingFeatures & filter) == filter;
}

void VideoTexture::create_ubo() {
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
        m->transformation = mat2x2::zeros();
        m->is_color_range_jpeg = true;
        m->is_color_space_bt709 = true;
    }
}

void VideoTexture::create_image_resources() {
    image_planes.resize(renderer.swap_chain->image_count());
    mapped_images.resize(renderer.swap_chain->image_count());
    mapped_image_layouts.resize(renderer.swap_chain->image_count());
    renderer.ctx.single_time_commands([this](const vk::UniqueCommandBuffer& cmd) {
        for (std::size_t i = 0; i < this->renderer.swap_chain->image_count(); i++) {
            for (std::size_t p = 0; p < 3; p++) {
                std::uint32_t width = this->width;
                std::uint32_t height = this->height;
                if (p > 0) {
                    width = div_ceil(width, 2);
                    height = div_ceil(height, 2);
                }
                vk::ImageUsageFlags usage_flags = vk::ImageUsageFlagBits::eSampled;
                if (is_image_transfer_supported)
                    usage_flags |= vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc;
                image_planes[i][p] = std::make_unique<Image>(
                    this->renderer.ctx,
                    vk::ImageType::e2D,
                    vk::Format::eR8Unorm,
                    vk::ImageTiling::eLinear,
                    vk::Extent3D(width, height, 1),
                    usage_flags,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
                image_planes[i][p]->transition_image_layout(
                    cmd,
                    vk::ImageLayout::eGeneral,
                    {},
                    {},
                    vk::PipelineStageFlagBits::eTopOfPipe,
                    vk::PipelineStageFlagBits::eAllCommands);
                image_planes[i][p]->map();
                mapped_image_layouts[i][p] = this->renderer.ctx.device->getImageSubresourceLayout(
                    *image_planes[i][p]->get_image(),
                    vk::ImageSubresource{ vk::ImageAspectFlagBits::eColor, 0, 0 });
                mapped_images[i][p] = static_cast<std::uint8_t*>(image_planes[i][p]->get_mapped_memory());
                if (is_image_transfer_supported) {
                    cmd->clearColorImage(
                        *image_planes[i][p]->get_image(),
                        image_planes[i][p]->get_layout(),
                        vk::ClearColorValue(p > 0 ? 0.5f : 0.f, 0.f, 0.f, 0.f),
                        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
                } else {
                    std::memset(
                        mapped_images[i][p],
                        p > 0 ? 128 : 0,
                        mapped_image_layouts[i][p].size);
                }
            }
        }
    });

    vk::Filter filter =
        (is_image_linear_filtering_supported)
            ? vk::Filter::eLinear
            : vk::Filter::eNearest;

    image_sampler = renderer.ctx.device->createSamplerUnique(vk::SamplerCreateInfo(
        {},
        filter,
        filter,
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
}

void VideoTexture::create_descriptors() {
    descriptor_set_layout =
        DescriptorSetLayout::Builder(renderer.ctx)
            .add_binding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
            .add_binding(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, &*image_sampler)
            .add_binding(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, &*image_sampler)
            .add_binding(3, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, &*image_sampler)
            .build();

    descriptor_sets.resize(renderer.swap_chain->image_count());
    for (std::size_t i = 0; i < renderer.swap_chain->image_count(); i++) {
        descriptor_sets[i] =
            DescriptorWriter(renderer.ctx, *descriptor_set_layout, *renderer.descriptor_pool)
                .write_buffer(0, ubo[i]->descriptor_info())
                .write_image(1, image_planes[i][0]->descriptor_info())
                .write_image(2, image_planes[i][1]->descriptor_info())
                .write_image(3, image_planes[i][2]->descriptor_info())
                .build();
    }
}

void VideoTexture::create_pipeline() {
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
        "shaders/dist/shader.vert.spv",
        "shaders/dist/shader.frag.spv",
        pipeline_config);
}

void VideoTexture::update(AVFrame* frame, const mat2x2& transform, std::uint32_t i) {
    update_ubo(transform, i);
    update_image(frame, i);
    prev_image_index = i;
}

void VideoTexture::draw(const vk::UniqueCommandBuffer& cmd, std::uint32_t i) {
    pipeline->bind(cmd);
    cmd->bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *pipeline_layout,
        0,
        *descriptor_sets[i],
        {});
    cmd->draw(4, 1, 0, 0);
}

void VideoTexture::update_ubo(const mat2x2& A, std::size_t i) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float W = static_cast<float>(renderer.swap_chain->width());
    const float H = static_cast<float>(renderer.swap_chain->height());

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

    const std::uint32_t box_width = static_cast<std::uint32_t>(xmax - xmin + 1);
    const std::uint32_t box_height = static_cast<std::uint32_t>(ymax - ymin + 1);

    float scale_to_fit = std::min(W / box_width, H / box_height);

    // [1, 1] -> [w/2, h/2]
    mat2x2 to_pixels_coord = mat2x2::zeros();
    to_pixels_coord[0][0] = w / 2.0f;
    to_pixels_coord[1][1] = h / 2.0f;

    // [W/2, H/2] -> [1, 1]
    mat2x2 norm = mat2x2::zeros();
    norm[0][0] = 2.0f / W;
    norm[1][1] = 2.0f / H;

    auto* v = static_cast<UBO*>(ubo[i]->get_mapped_memory());
    v->transformation = norm * scale_to_fit * A * to_pixels_coord;
}

void VideoTexture::update_image(AVFrame* frame, std::uint32_t i) {
    if (frame == nullptr) {
        if (prev_image_index == std::numeric_limits<std::uint32_t>::max()) return;
        if (prev_image_index == i) return;
        renderer.ctx.single_time_commands([this, i](vk::UniqueCommandBuffer& cmd) {
            for (std::size_t p = 0; p < 3; p++) {
                if (is_image_transfer_supported) {
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
                } else {
                    std::uint8_t* src = mapped_images[prev_image_index][p] + mapped_image_layouts[prev_image_index][p].offset;
                    std::size_t src_pitch = mapped_image_layouts[prev_image_index][p].rowPitch;
                    std::uint8_t* dst = mapped_images[i][p] + mapped_image_layouts[i][p].offset;
                    std::size_t dst_pitch = mapped_image_layouts[i][p].rowPitch;
                    std::size_t height = std::min(
                        static_cast<std::size_t>(image_planes[i][p]->get_extent().height),
                        static_cast<std::size_t>(image_planes[prev_image_index][p]->get_extent().height));
                    for (std::size_t i = 0; i < height; i++) {
                        std::memcpy(dst + i * dst_pitch,
                                    src + i * src_pitch,
                                    std::min(dst_pitch, src_pitch));
                    }
                }
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
    auto* v = static_cast<UBO*>(ubo[i]->get_mapped_memory());
    v->is_color_range_jpeg = frame->color_range == AVCOL_RANGE_JPEG;
    v->is_color_space_bt709 = frame->colorspace == AVCOL_SPC_BT709;
}

}  // namespace aglea