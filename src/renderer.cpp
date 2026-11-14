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
#include "renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vulkan/vulkan.hpp>

#include "descriptors.hpp"
#include "swap_chain.hpp"
#include "window.hpp"

namespace aglea {

Renderer::Renderer(Window& window) : window(window), ctx(window) {
    create_swap_chain();
    create_descriptor_pool();
    create_command_buffers();
}

Renderer::~Renderer() {
    ctx.device->waitIdle();
}

void Renderer::create_swap_chain() {
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
        create_descriptor_pool();
        create_command_buffers();
        for (auto* texture : textures) {
            texture->init();
        }
    }
    old_swap_chain = nullptr;
}

void Renderer::create_descriptor_pool() {
    descriptor_pool =
        DescriptorPool::Builder(ctx)
            .set_max_sets(swap_chain->image_count() * 2)
            .add_pool_size(vk::DescriptorType::eUniformBuffer, swap_chain->image_count() * 2)
            .add_pool_size(vk::DescriptorType::eCombinedImageSampler, swap_chain->image_count() * 3)
            .build();
}

void Renderer::create_command_buffers() {
    command_buffers.resize(swap_chain->image_count());
    auto cmd = ctx.create_command_buffers(command_buffers.size());
    for (std::size_t i = 0; i < command_buffers.size(); i++) {
        command_buffers[i].first = false;
        command_buffers[i].second = std::move(cmd[i]);
    }
}

void Renderer::invalidate_command_buffers() {
    for (auto&& p : command_buffers) p.first = false;
}

std::optional<std::uint32_t> Renderer::acquire_next_frame() {
    try {
        std::uint32_t i = swap_chain->acquire_next_image();
        return i;
    } catch (const vk::OutOfDateKHRError&) {
        window.reset_window_resized_flag();
        create_swap_chain();
        invalidate_command_buffers();
        return {};
    }
}

void Renderer::submit_frame(std::uint32_t i) {
    try {
        auto res = swap_chain->submit_command_buffer(command_buffers[i].second, i);
        if (res == vk::Result::eSuboptimalKHR || window.was_window_resized()) {
            window.reset_window_resized_flag();
            create_swap_chain();
            invalidate_command_buffers();
        }
    } catch (const vk::OutOfDateKHRError&) {
        window.reset_window_resized_flag();
        create_swap_chain();
        invalidate_command_buffers();
    }
}

void Renderer::record_command_buffer(std::uint32_t i) {
    if (!command_buffers[i].first) {
        auto& cmd = command_buffers[i].second;
        cmd->begin(vk::CommandBufferBeginInfo());

        for (auto* texture : textures) {
            texture->pre(cmd, i);
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

        for (auto* texture : textures) {
            texture->draw(cmd, i);
        }

        cmd->endRenderPass();
        cmd->end();
        command_buffers[i].first = true;
    }
}

}  // namespace aglea