#pragma once
#include <cstddef>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "context.hpp"

namespace aglea {

class SwapChain {
    vk::Format swap_chain_image_format;
    vk::Format swap_chain_depth_format;
    vk::Extent2D swap_chain_extent;
    std::vector<vk::UniqueFramebuffer> frame_buffers;
    vk::UniqueRenderPass render_pass;

    std::vector<vk::UniqueImage> depth_images;
    std::vector<vk::UniqueDeviceMemory> depth_image_memorys;
    std::vector<vk::UniqueImageView> depth_image_views;
    std::vector<vk::Image> swap_chain_images;
    std::vector<vk::UniqueImageView> swap_chain_image_views;

    const Context& ctx;
    vk::Extent2D window_extent;

    vk::UniqueSwapchainKHR swap_chain;

    std::vector<vk::UniqueSemaphore> image_available_semaphores;
    std::vector<vk::UniqueSemaphore> render_finished_semaphores;
    std::vector<vk::UniqueFence> in_flight_fences;
    std::vector<vk::Fence> images_in_flight;
    std::size_t current_frame = 0;

   public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    SwapChain(const Context& ctx, vk::Extent2D extent, const SwapChain* old_swap_chain = nullptr);

    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    const vk::UniqueFramebuffer& get_frame_buffer(int index) const { return frame_buffers[index]; }
    std::size_t current_frame_index() const { return current_frame; }
    const vk::UniqueRenderPass& get_render_pass() const { return render_pass; }
    const vk::UniqueImageView& get_image_view(int index) const { return swap_chain_image_views[index]; }
    std::size_t image_count() const { return swap_chain_images.size(); }
    vk::Format get_swap_chain_image_format() const { return swap_chain_image_format; }
    vk::Extent2D get_swap_chain_extent() const { return swap_chain_extent; }
    std::uint32_t width() const { return swap_chain_extent.width; }
    std::uint32_t height() const { return swap_chain_extent.height; }

    vk::Result acquire_next_image(std::uint32_t& image_index);
    vk::Result submit_command_buffer(const vk::UniqueCommandBuffer& cmd, std::uint32_t image_index);

    vk::Format find_depth_format();

    bool cmpeq_swap_formats(const SwapChain& o) const {
        return swap_chain_depth_format == o.swap_chain_depth_format &&
               swap_chain_image_format == o.swap_chain_image_format;
    }

   private:
    void create_swap_chain(const SwapChain* old_swap_chain);
    void create_image_views();
    void create_depth_resources();
    void create_render_pass();
    void create_framebuffers();
    void create_sync_objects();

    vk::SurfaceFormatKHR choose_swap_surface_format(
        const std::vector<vk::SurfaceFormatKHR>& available_formats);
    vk::PresentModeKHR choose_swap_present_mode(
        const std::vector<vk::PresentModeKHR>& available_present_modes);
    vk::Extent2D choose_swap_extent(const vk::SurfaceCapabilitiesKHR& capabilities);
};

}  // namespace aglea
