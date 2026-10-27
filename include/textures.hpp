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
