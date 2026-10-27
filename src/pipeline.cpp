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
#include "pipeline.hpp"

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace aglea {

void GraphicsPipeline::bind(const vk::UniqueCommandBuffer& command_buffer) {
    command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

GraphicsPipeline::GraphicsPipeline(
    const Context& ctx,
    const vk::UniquePipelineLayout& pipeline_layout,
    const vk::UniqueRenderPass& render_pass,
    std::uint32_t subpass,
    std::span<const std::uint32_t> vertex_shader_code,
    std::span<const std::uint32_t> fragment_shader_code,
    const GraphicsPipelineConfigInfo& config) {
    vertex_shader_module = ctx.device->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo({}, vertex_shader_code));
    fragment_shader_module = ctx.device->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo({}, fragment_shader_code));

    vk::PipelineShaderStageCreateInfo shader_stages[2];

    shader_stages[0] = {
        {},
        vk::ShaderStageFlagBits::eVertex,
        *vertex_shader_module,
        "main",
    };

    shader_stages[1] = {
        {},
        vk::ShaderStageFlagBits::eFragment,
        *fragment_shader_module,
        "main",
    };

    vk::PipelineVertexInputStateCreateInfo vertex_input_info{};

    vk::GraphicsPipelineCreateInfo create_info(
        {},
        shader_stages,
        &vertex_input_info,
        &config.input_assembly,
        nullptr,
        &config.viewport,
        &config.rasterization,
        &config.multisample,
        &config.depth_stencil,
        &config.color_blend,
        &config.dynamic_state,
        *pipeline_layout,
        *render_pass,
        subpass,
        {},
        -1);

    pipeline = ctx.device->createGraphicsPipelineUnique(vk::PipelineCache(), create_info).value;
}

GraphicsPipelineConfigInfo GraphicsPipeline::default_config() {
    GraphicsPipelineConfigInfo config = {};

    config.input_assembly.topology = vk::PrimitiveTopology::eTriangleList;
    config.input_assembly.primitiveRestartEnable = vk::False;

    config.viewport.viewportCount = 1;
    config.viewport.pViewports = nullptr;
    config.viewport.scissorCount = 1;
    config.viewport.pScissors = nullptr;

    config.rasterization.depthClampEnable = vk::False;
    config.rasterization.rasterizerDiscardEnable = vk::False;
    config.rasterization.polygonMode = vk::PolygonMode::eFill;
    config.rasterization.lineWidth = 1.0f;
    config.rasterization.cullMode = vk::CullModeFlagBits::eNone;
    config.rasterization.frontFace = vk::FrontFace::eClockwise;
    config.rasterization.depthBiasEnable = vk::False;
    config.rasterization.depthBiasConstantFactor = 0.0f;  // Optional
    config.rasterization.depthBiasClamp = 0.0f;           // Optional
    config.rasterization.depthBiasSlopeFactor = 0.0f;     // Optional

    config.multisample.sampleShadingEnable = vk::False;
    config.multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;
    config.multisample.minSampleShading = 1.0f;            // Optional
    config.multisample.pSampleMask = nullptr;              // Optional
    config.multisample.alphaToCoverageEnable = vk::False;  // Optional
    config.multisample.alphaToOneEnable = vk::False;       // Optional

    config.color_blend_attachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    config.color_blend_attachment.blendEnable = vk::False;
    config.color_blend_attachment.srcColorBlendFactor = vk::BlendFactor::eOne;   // Optional
    config.color_blend_attachment.dstColorBlendFactor = vk::BlendFactor::eZero;  // Optional
    config.color_blend_attachment.colorBlendOp = vk::BlendOp::eAdd;              // Optional
    config.color_blend_attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;   // Optional
    config.color_blend_attachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;  // Optional
    config.color_blend_attachment.alphaBlendOp = vk::BlendOp::eAdd;              // Optional

    config.color_blend.logicOpEnable = vk::False;
    config.color_blend.logicOp = vk::LogicOp::eCopy;  // Optional
    config.color_blend.attachmentCount = 1;
    config.color_blend.pAttachments = &config.color_blend_attachment;
    config.color_blend.blendConstants[0] = 0.0f;  // Optional
    config.color_blend.blendConstants[1] = 0.0f;  // Optional
    config.color_blend.blendConstants[2] = 0.0f;  // Optional
    config.color_blend.blendConstants[3] = 0.0f;  // Optional

    config.depth_stencil.depthTestEnable = vk::True;
    config.depth_stencil.depthWriteEnable = vk::True;
    config.depth_stencil.depthCompareOp = vk::CompareOp::eLess;
    config.depth_stencil.depthBoundsTestEnable = vk::False;
    config.depth_stencil.minDepthBounds = 0.0f;  // Optional
    config.depth_stencil.maxDepthBounds = 1.0f;  // Optional
    config.depth_stencil.stencilTestEnable = vk::False;
    config.depth_stencil.front = vk::StencilOpState();  // Optional
    config.depth_stencil.back = vk::StencilOpState();   // Optional

    config.dynamic_state_enables = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    config.dynamic_state = vk::PipelineDynamicStateCreateInfo({}, config.dynamic_state_enables);

    return config;
}

}  // namespace aglea
