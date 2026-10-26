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
