#include "context.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace aglea {

Context::Context(const Window& window) : window{ window } {
    create_instance();
    create_surface();
    pick_physical_device();
    create_logical_device();
    create_command_pool();
}

void Context::create_instance() {
    auto extensions = get_required_extensions();
    instance = vk::createInstanceUnique(
        vk::InstanceCreateInfo({}, &app_info, validation_layers, extensions));
}

std::vector<const char*> Context::get_required_extensions() {
    std::uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (validation_layers.size() > 0)
        extensions.push_back(vk::EXTDebugUtilsExtensionName);

    return extensions;
}

void Context::create_surface() {
    surface = window.create_window_surface(instance);
}

void Context::pick_physical_device() {
    for (const auto& device : instance->enumeratePhysicalDevices()) {
        if (is_device_suitable(device)) {
            physical_device = vk::UniquePhysicalDevice(device);
            break;
        }
    }
    physical_device->getProperties(&properties);
}

bool Context::is_device_suitable(const vk::PhysicalDevice& physical_device) {
    QueueFamilyIndices indices = find_queue_families(physical_device);

    bool extensions_supported = check_device_extension_support(physical_device);

    bool swap_chain_adequate = false;
    if (extensions_supported) {
        SwapChainSupportDetails swapChainSupport = query_swap_chain_support(physical_device);
        swap_chain_adequate = !swapChainSupport.formats.empty() && !swapChainSupport.present_modes.empty();
    }

    auto suppored_features = physical_device.getFeatures();

    return indices.is_complete() && extensions_supported &&
           swap_chain_adequate && suppored_features.samplerAnisotropy;
}

Context::QueueFamilyIndices Context::find_queue_families(const vk::PhysicalDevice& physical_device) {
    QueueFamilyIndices indices = {};

    std::uint32_t i = 0;

    for (const auto& queueFamily : physical_device.getQueueFamilyProperties()) {
        if (queueFamily.queueCount > 0 && queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphics_family = i;
            indices.graphics_family_has_value = true;
        }
        if (queueFamily.queueCount > 0 && physical_device.getSurfaceSupportKHR(i, *surface)) {
            indices.present_family = i;
            indices.present_family_has_value = true;
        }
        if (indices.is_complete()) {
            break;
        }

        i++;
    }

    return indices;
}

bool Context::check_device_extension_support(const vk::PhysicalDevice& device) {
    auto available_extensions = device.enumerateDeviceExtensionProperties();
    std::set<std::string>
        required_extensions_set(device_extensions.begin(), device_extensions.end());
    for (auto&& available_extension : device.enumerateDeviceExtensionProperties())
        required_extensions_set.erase(available_extension.extensionName);
    return required_extensions_set.empty();
}

SwapChainSupportDetails Context::query_swap_chain_support(const vk::PhysicalDevice& physical_device) const {
    SwapChainSupportDetails details;
    details.capabilities = physical_device.getSurfaceCapabilitiesKHR(*surface);
    details.formats = physical_device.getSurfaceFormatsKHR(*surface);
    details.present_modes = physical_device.getSurfacePresentModesKHR(*surface);
    return details;
}

void Context::create_logical_device() {
    QueueFamilyIndices indices = find_queue_families(*physical_device);
    graphics_family_index = indices.graphics_family;
    present_family_index = indices.present_family;

    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos = {};
    float queue_priority = 1.0f;
    for (std::uint32_t queue_family : std::set{ indices.graphics_family, indices.present_family }) {
        queue_create_infos.push_back(vk::DeviceQueueCreateInfo({}, queue_family, 1, &queue_priority));
    }

    vk::PhysicalDeviceFeatures device_features = {};
    device_features.samplerAnisotropy = vk::True;

    device =
        physical_device->createDeviceUnique(
            vk::DeviceCreateInfo({}, queue_create_infos, {}, device_extensions, &device_features));

    graphics_queue = vk::UniqueQueue(device->getQueue(graphics_family_index, 0));
    present_queue = vk::UniqueQueue(device->getQueue(present_family_index, 0));
}

void Context::create_command_pool() {
    command_pool =
        device->createCommandPoolUnique(
            vk::CommandPoolCreateInfo(
                vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                graphics_family_index));
}

std::uint32_t Context::find_memory_type(std::uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
    vk::PhysicalDeviceMemoryProperties memProperties = physical_device->getMemoryProperties();
    for (std::uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

}  // namespace aglea
