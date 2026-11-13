/*
    media-player
    Copyright (C) 2026  miyabi-no1-fan

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "textures/video_texture.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
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

static constexpr std::uint8_t vertex_shader[] = {
#embed "../../shaders/dist/shader.vert.spv"
};
static constexpr std::uint8_t fragment_shader[] = {
#embed "../../shaders/dist/shader.frag.spv"
};
static_assert(sizeof(vertex_shader) % sizeof(std::uint32_t) == 0);
static_assert(sizeof(fragment_shader) % sizeof(std::uint32_t) == 0);

void VideoTexture::init() {
    create_ubo();
    create_image_resources();
    create_descriptors();
    create_pipeline();
}

void VideoTexture::create_ubo() {
    ubo.resize(renderer.swap_chain->image_count());
    for (auto&& v : ubo) {
        v = std::make_unique<Buffer>(
            renderer.ctx,
            sizeof(UBO), 1,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        v->map();
        auto* m = static_cast<UBO*>(v->get_mapped_memory());
        m->transformation = mat2x2::zeros();
        m->is_color_range_jpeg = true;
        m->is_color_space_bt709 = true;
    }
}

std::size_t VideoTexture::get_width(std::size_t plane) {
    if (plane == 0) return static_cast<std::size_t>(width);
    return static_cast<std::size_t>(div_ceil(width, 2));
}

std::size_t VideoTexture::get_height(std::size_t plane) {
    if (plane == 0) return static_cast<std::size_t>(height);
    return static_cast<std::size_t>(div_ceil(height, 2));
}

void VideoTexture::create_image_resources() {
    image_planes.resize(renderer.swap_chain->image_count());
    staging_buffers.resize(renderer.swap_chain->image_count());
    renderer.ctx.single_time_commands([this](const vk::UniqueCommandBuffer& cmd) {
        for (std::size_t i = 0; i < renderer.swap_chain->image_count(); i++) {
            for (std::size_t p = 0; p < VideoTexture::NPLANES; p++) {
                image_planes[i][p] = std::make_unique<Image>(
                    renderer.ctx,
                    vk::ImageType::e2D, vk::Format::eR8Unorm,
                    vk::Extent3D(get_width(p), get_height(p), 1),
                    vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
                    vk::MemoryPropertyFlagBits::eDeviceLocal);

                vk::ImageMemoryBarrier barrier(
                    vk::AccessFlagBits::eNone, vk::AccessFlagBits::eNone,
                    vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
                    renderer.ctx.graphics_family_index, renderer.ctx.graphics_family_index,
                    *image_planes[i][p]->get_image(),
                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

                cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                     vk::PipelineStageFlagBits::eBottomOfPipe,
                                     {}, nullptr, nullptr, barrier);

                staging_buffers[i][p] = std::make_unique<Buffer>(
                    renderer.ctx,
                    1, get_height(p) * get_width(p),
                    vk::BufferUsageFlagBits::eTransferSrc,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

                staging_buffers[i][p]->map();

                std::memset(staging_buffers[i][p]->get_mapped_memory(),
                            p == 0 ? 0 : 128, staging_buffers[i][p]->get_buffer_size());
            }
        }
    });

    image_sampler = renderer.ctx.device->createSamplerUnique(vk::SamplerCreateInfo(
        {}, vk::Filter::eLinear, vk::Filter::eLinear,
        vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge,
        0, vk::False, 0, vk::False, vk::CompareOp::eNever, 0, 0,
        vk::BorderColor::eFloatOpaqueBlack, vk::False));
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
                .write_image(1, image_planes[i][0]->descriptor_info(vk::ImageLayout::eShaderReadOnlyOptimal))
                .write_image(2, image_planes[i][1]->descriptor_info(vk::ImageLayout::eShaderReadOnlyOptimal))
                .write_image(3, image_planes[i][2]->descriptor_info(vk::ImageLayout::eShaderReadOnlyOptimal))
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
        std::span<const std::uint32_t>(
            reinterpret_cast<const std::uint32_t*>(vertex_shader),
            sizeof(vertex_shader) / sizeof(std::uint32_t)),
        std::span<const std::uint32_t>(
            reinterpret_cast<const std::uint32_t*>(fragment_shader),
            sizeof(fragment_shader) / sizeof(std::uint32_t)),
        pipeline_config);
}

void VideoTexture::update(AVFrame* frame, const mat2x2& transform, std::uint32_t i) {
    update_ubo(transform, i);
    update_image(frame, i);
}

void VideoTexture::pre(const vk::UniqueCommandBuffer& cmd, std::uint32_t i) {
    for (std::size_t p = 0; p < VideoTexture::NPLANES; p++) {
        vk::ImageMemoryBarrier image_before(
            vk::AccessFlagBits::eNone, vk::AccessFlagBits::eTransferWrite,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal,
            renderer.ctx.graphics_family_index, renderer.ctx.graphics_family_index,
            *image_planes[i][p]->get_image(),
            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

        cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                             vk::PipelineStageFlagBits::eTransfer,
                             {}, {}, {}, image_before);

        cmd->copyBufferToImage(
            *staging_buffers[i][p]->get_buffer(),
            *image_planes[i][p]->get_image(), vk::ImageLayout::eTransferDstOptimal,
            vk::BufferImageCopy(0, get_width(p), get_height(p),
                                vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
                                vk::Offset3D(0, 0, 0), image_planes[i][p]->get_extent()));

        vk::ImageMemoryBarrier image_after(
            vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            renderer.ctx.graphics_family_index, renderer.ctx.graphics_family_index,
            *image_planes[i][p]->get_image(),
            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

        cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                             vk::PipelineStageFlagBits::eFragmentShader,
                             {}, {}, {}, image_after);
    }
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
    if (frame == nullptr) return;
    if (frame->format == AV_PIX_FMT_YUV420P) {
        for (std::size_t p = 0; p < VideoTexture::NPLANES; p++) {
            std::uint8_t* dst = static_cast<std::uint8_t*>(staging_buffers[i][p]->get_mapped_memory());
            std::size_t dst_stride = get_width(p);
            std::uint8_t* src = frame->data[p];
            std::size_t src_stride = static_cast<std::size_t>(frame->linesize[p]);
            std::size_t h = std::min(get_height(p), static_cast<std::size_t>(p == 0 ? frame->height : div_ceil(frame->height, 2)));
            std::size_t w = std::min(get_width(p), static_cast<std::size_t>(p == 0 ? frame->width : div_ceil(frame->width, 2)));
            for (std::size_t i = 0; i < h; i++)
                std::memcpy(dst + i * dst_stride, src + i * src_stride, w);
        }
    } else if (frame->format == AV_PIX_FMT_NV12) {
        {
            std::uint8_t* dst = static_cast<std::uint8_t*>(staging_buffers[i][0]->get_mapped_memory());
            std::size_t dst_stride = get_width(0);
            std::uint8_t* src = frame->data[0];
            std::size_t src_stride = static_cast<std::size_t>(frame->linesize[0]);
            std::size_t h = std::min(get_height(0), static_cast<std::size_t>(frame->height));
            std::size_t w = std::min(get_width(0), static_cast<std::size_t>(frame->width));
            for (std::size_t i = 0; i < h; i++)
                std::memcpy(dst + i * dst_stride, src + i * src_stride, w);
        }
        {
            std::uint8_t* U = static_cast<std::uint8_t*>(staging_buffers[i][1]->get_mapped_memory());
            std::uint8_t* V = static_cast<std::uint8_t*>(staging_buffers[i][2]->get_mapped_memory());
            std::size_t U_stride = get_width(1);
            std::size_t V_stride = get_width(2);
            std::uint8_t* src = frame->data[1];
            std::size_t src_stride = static_cast<std::size_t>(frame->linesize[1]);
            std::size_t h = std::min(std::min(get_height(1), get_height(2)), static_cast<std::size_t>(div_ceil(frame->height, 2)));
            std::size_t w = std::min(std::min(get_width(1), get_width(2)), static_cast<std::size_t>(div_ceil(frame->width, 2)));
            for (std::size_t i = 0; i < h; i++) {
                for (std::size_t j = 0; j < w; j++) {
                    U[i * U_stride + j] = src[i * src_stride + j * 2 + 0];
                    V[i * V_stride + j] = src[i * src_stride + j * 2 + 1];
                }
            }
        }
    } else {
        throw std::runtime_error("Unknown pixel format.");
    }
    auto* v = static_cast<UBO*>(ubo[i]->get_mapped_memory());
    v->is_color_range_jpeg = frame->color_range == AVCOL_RANGE_JPEG;
    v->is_color_space_bt709 = frame->colorspace == AVCOL_SPC_BT709;
}

}  // namespace aglea