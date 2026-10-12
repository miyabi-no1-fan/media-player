#include "pipeline.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace aglea {

void GraphicsPipeline::bind(const vk::UniqueCommandBuffer& command_buffer) {
    command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

std::vector<std::uint32_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Can't open file " + path);

    std::size_t len = static_cast<std::size_t>(file.tellg());
    if (len % 4 != 0)
        throw std::runtime_error("File " + path + "is not a valid spv file");

    std::vector<std::uint32_t> content(len / 4);
    file.seekg(0).read(reinterpret_cast<char*>(content.data()), len);

    return content;
}

GraphicsPipeline::GraphicsPipeline(
    const Context& ctx,
    const vk::UniquePipelineLayout& pipeline_layout,
    const vk::UniqueRenderPass& render_pass,
    std::uint32_t subpass,
    const std::string& vertex_shader_path,
    const std::string& fragment_shader_path,
    const GraphicsPipelineConfigInfo& config) {
    auto vertex_shader_code = read_file(vertex_shader_path);
    auto fragment_shader_code = read_file(fragment_shader_path);

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
