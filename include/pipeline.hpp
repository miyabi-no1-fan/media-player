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
#include <vector>
#include <vulkan/vulkan.hpp>

#include "context.hpp"

namespace aglea {

struct GraphicsPipelineConfigInfo {
    vk::PipelineViewportStateCreateInfo viewport;
    vk::PipelineInputAssemblyStateCreateInfo input_assembly;
    vk::PipelineRasterizationStateCreateInfo rasterization;
    vk::PipelineMultisampleStateCreateInfo multisample;
    vk::PipelineColorBlendAttachmentState color_blend_attachment;
    vk::PipelineColorBlendStateCreateInfo color_blend;
    vk::PipelineDepthStencilStateCreateInfo depth_stencil;
    std::vector<vk::DynamicState> dynamic_state_enables;
    vk::PipelineDynamicStateCreateInfo dynamic_state;

    GraphicsPipelineConfigInfo() {}
    GraphicsPipelineConfigInfo(const GraphicsPipelineConfigInfo&) = delete;
    GraphicsPipelineConfigInfo& operator=(const GraphicsPipelineConfigInfo&) = delete;
    GraphicsPipelineConfigInfo(GraphicsPipelineConfigInfo&&) = default;
    GraphicsPipelineConfigInfo& operator=(GraphicsPipelineConfigInfo&&) = default;
};

class GraphicsPipeline {
   public:
    vk::UniquePipeline pipeline;
    vk::UniqueShaderModule vertex_shader_module;
    vk::UniqueShaderModule fragment_shader_module;

    GraphicsPipeline(
        const Context& ctx,
        const vk::UniquePipelineLayout& pipeline_layout,
        const vk::UniqueRenderPass& render_pass,
        std::uint32_t subpass,
        std::span<const std::uint32_t> vertex_shader_code,
        std::span<const std::uint32_t> fragment_shader_code,
        const GraphicsPipelineConfigInfo& config);

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    static GraphicsPipelineConfigInfo default_config();

    void bind(const vk::UniqueCommandBuffer& command_buffer);
};

}  // namespace aglea
