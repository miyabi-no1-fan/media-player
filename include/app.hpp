#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
extern "C" {
#include <libavutil/frame.h>
}

#include "buffer.hpp"
#include "context.hpp"
#include "descriptors.hpp"
#include "image.hpp"
#include "matrix.hpp"
#include "pipeline.hpp"
#include "progress_bar.hpp"
#include "swap_chain.hpp"
#include "window.hpp"

namespace aglea {

struct GlobalUBO {
    mat2x2 transformation = mat2x2::identity();
    std::uint32_t is_color_range_jpeg;
    std::uint32_t is_color_space_bt709;
};

class App {
    //
   public:
    static constexpr std::uint32_t DEFAULT_WIDTH = 1920;
    static constexpr std::uint32_t DEFAULT_HEIGHT = 1080;
    static constexpr const char* NAME = "Aglea";

   private:
    std::uint32_t width;  // the video's width and height
    std::uint32_t height;

    Window window;
    Context ctx;

    std::unique_ptr<SwapChain> swap_chain{};
    std::vector<std::pair<bool, vk::UniqueCommandBuffer>> command_buffers{};

    std::unique_ptr<DescriptorPool> descriptor_pool;
    std::unique_ptr<DescriptorSetLayout> descriptor_set_layout;
    vk::UniquePipelineLayout graphics_pipeline_layout;
    std::unique_ptr<GraphicsPipeline> graphics_pipeline;

    std::vector<std::unique_ptr<Buffer>> global_ubo;
    std::vector<std::array<std::unique_ptr<Image>, 3>> image_planes;
    vk::UniqueSampler image_sampler;
    std::vector<std::array<std::uint8_t*, 3>> mapped_images;
    std::vector<std::array<vk::SubresourceLayout, 3>> mapped_image_layouts;
    std::vector<vk::UniqueDescriptorSet> descriptor_sets;

    std::uint32_t prev_image_index = std::numeric_limits<std::uint32_t>::max();

    bool is_image_transfer_supported = false;
    bool is_image_linear_filtering_supported = false;

    ProgressBar progress_bar;

   public:
    App(std::uint32_t width, std::uint32_t height, double frame_time, const std::string& name);
    ~App();

    void render(AVFrame* frame, float progress, mat2x2 transformation = mat2x2::identity());
    bool should_close() const { return window.should_close(); }
    GLFWwindow* get_window() const { return window.get_window(); }

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

   private:
    void create_swap_chain();
    void update_global_ubo(std::size_t i, mat2x2 transformation);
    void draw(AVFrame* frame, std::uint32_t i);
    void record_command_buffer(std::uint32_t i);
    std::optional<std::uint32_t> acquire_next_frame();
    void submit_frame(std::uint32_t i);
};

}  // namespace aglea
