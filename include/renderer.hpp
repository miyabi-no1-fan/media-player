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
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "context.hpp"
#include "descriptors.hpp"
#include "swap_chain.hpp"
#include "textures.hpp"
#include "window.hpp"

namespace aglea {

class Renderer {
   public:
    Window& window;
    Context ctx;
    std::unique_ptr<SwapChain> swap_chain{};
    std::vector<std::pair<bool, vk::UniqueCommandBuffer>> command_buffers{};
    std::unique_ptr<DescriptorPool> descriptor_pool;

    // if textures change, call `invalidate_command_buffers`
    std::vector<aglea::Texture*> textures;

    Renderer(Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    std::optional<std::uint32_t> acquire_next_frame();
    void record_command_buffer(std::uint32_t i);
    void invalidate_command_buffers();
    void submit_frame(std::uint32_t i);

   private:
    void create_swap_chain();
    void create_command_buffers();
    void create_descriptor_pool();
};

}  // namespace aglea
