#pragma once
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "window.hpp"

namespace aglea {

struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> present_modes;
};

class Context {
    #ifdef DNDEBUG
    const std::vector<const char*> validation_layers = {};
    #else
    const std::vector<const char*> validation_layers = { "VK_LAYER_KHRONOS_validation" };
    #endif

    const std::vector<const char*> device_extensions = { vk::KHRSwapchainExtensionName };

    const Window& window;

   public:
    vk::ApplicationInfo app_info{
        nullptr,
        vk::makeApiVersion(0, 0, 0, 0),
        nullptr,
        vk::makeApiVersion(0, 0, 0, 0),
        vk::ApiVersion10,
    };
    vk::UniqueInstance instance;
    vk::UniquePhysicalDevice physical_device;
    vk::PhysicalDeviceProperties properties;
    std::uint32_t graphics_family_index;
    std::uint32_t present_family_index;
    vk::UniqueDevice device;
    vk::UniqueQueue graphics_queue;
    vk::UniqueQueue present_queue;
    vk::UniqueCommandPool command_pool;
    vk::UniqueSurfaceKHR surface;

    Context(const Window& window);

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    SwapChainSupportDetails get_swap_chain_support() const { return query_swap_chain_support(*physical_device); }

    std::uint32_t find_memory_type(std::uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;

    std::vector<vk::UniqueCommandBuffer>
    create_command_buffers(
        std::uint32_t count,
        vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary) const {
        return device->allocateCommandBuffersUnique(
            vk::CommandBufferAllocateInfo(
                *command_pool,
                level,
                count));
    }

    // void f(vk::UniqueCommandBuffer& cmd);
    template <typename F>
    void single_time_commands(F&& f) const {
        vk::UniqueCommandBuffer cmd =
            std::move(device->allocateCommandBuffersUnique(
                vk::CommandBufferAllocateInfo(
                    *command_pool,
                    vk::CommandBufferLevel::ePrimary,
                    1))[0]);

        cmd->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        (void)f(cmd);
        cmd->end();

        vk::UniqueFence fence = device->createFenceUnique({});
        graphics_queue->submit(vk::SubmitInfo({}, {}, *cmd), *fence);
        if (device->waitForFences(*fence, vk::True, UINT64_MAX) != vk::Result::eSuccess)
            throw std::runtime_error("device->waitForFences failed.");
    }

   private:
    void create_instance();
    void create_surface();
    void pick_physical_device();
    void create_logical_device();
    void create_command_pool();

    struct QueueFamilyIndices {
        std::uint32_t graphics_family;
        std::uint32_t present_family;
        bool graphics_family_has_value = false;
        bool present_family_has_value = false;
        bool is_complete() { return graphics_family_has_value & present_family_has_value; }
    };

    bool is_device_suitable(const vk::PhysicalDevice& device);
    std::vector<const char*> get_required_extensions();
    QueueFamilyIndices find_queue_families(const vk::PhysicalDevice& device);
    bool check_device_extension_support(const vk::PhysicalDevice& device);
    SwapChainSupportDetails query_swap_chain_support(const vk::PhysicalDevice& device) const;
};

}  // namespace aglea
