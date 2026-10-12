#include "swap_chain.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan.hpp>

namespace aglea {

SwapChain::SwapChain(const Context& ctx, vk::Extent2D extent, const SwapChain* old_swap_chain)
    : ctx(ctx), window_extent(extent) {
    assert(this != old_swap_chain && "recreate swap chain in place is UB");
    create_swap_chain(old_swap_chain);
    create_image_views();
    create_render_pass();
    create_depth_resources();
    create_framebuffers();
    create_sync_objects();
}

vk::Result SwapChain::acquire_next_image(std::uint32_t& image_index) {
    auto res = ctx.device->waitForFences(
        *in_flight_fences[current_frame],
        vk::True,
        std::numeric_limits<std::uint64_t>::max());
    if (res != vk::Result::eSuccess)
        throw std::runtime_error("SwapChain::acquire_next_image: waitForFences failed. Vulkan Error code: " + std::to_string((int)res));

    res = ctx.device->acquireNextImageKHR(
        *swap_chain,
        std::numeric_limits<std::uint64_t>::max(),
        *image_available_semaphores[current_frame],
        {},
        &image_index);

    return res;
}

vk::Result SwapChain::submit_command_buffer(const vk::UniqueCommandBuffer& buffer, std::uint32_t image_index) {
    if (images_in_flight[image_index]) {
        auto res = ctx.device->waitForFences(images_in_flight[image_index], vk::True, std::numeric_limits<std::uint64_t>::max());
        if (res != vk::Result::eSuccess)
            throw std::runtime_error("SwapChain::submint_command_buffer: waitForFences failed. Vulkan Error code: " + std::to_string((int)res));
    }
    images_in_flight[image_index] = *in_flight_fences[current_frame];

    vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

    ctx.device->resetFences(*in_flight_fences[current_frame]);
    ctx.graphics_queue->submit(
        vk::SubmitInfo(
            *image_available_semaphores[current_frame],
            waitStages,
            *buffer,
            *render_finished_semaphores[image_index]),
        *in_flight_fences[current_frame]);
    auto res = ctx.present_queue->presentKHR(vk::PresentInfoKHR(
        *render_finished_semaphores[image_index],
        *swap_chain,
        image_index,
        {}));

    current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

    return res;
}

void SwapChain::create_swap_chain(const SwapChain* old_swap_chain) {
    SwapChainSupportDetails swap_chain_support = ctx.get_swap_chain_support();

    vk::SurfaceFormatKHR surface_format = choose_swap_surface_format(swap_chain_support.formats);
    vk::PresentModeKHR present_mode = choose_swap_present_mode(swap_chain_support.present_modes);
    vk::Extent2D extent = choose_swap_extent(swap_chain_support.capabilities);

    std::uint32_t image_count = swap_chain_support.capabilities.minImageCount + 1;
    if (swap_chain_support.capabilities.maxImageCount > 0 &&
        image_count > swap_chain_support.capabilities.maxImageCount) {
        image_count = swap_chain_support.capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR create_info(
        {},
        *ctx.surface,
        image_count,
        surface_format.format,
        surface_format.colorSpace,
        extent,
        1,
        vk::ImageUsageFlagBits::eColorAttachment);

    if (ctx.graphics_family_index != ctx.present_family_index) {
        std::uint32_t queue_family_indices[] = { ctx.graphics_family_index, ctx.present_family_index };
        create_info.setImageSharingMode(vk::SharingMode::eConcurrent);
        create_info.setQueueFamilyIndices(queue_family_indices);
    } else {
        create_info.setImageSharingMode(vk::SharingMode::eExclusive);
        create_info.setQueueFamilyIndices({});  // optional
    }

    create_info.setPreTransform(swap_chain_support.capabilities.currentTransform);
    create_info.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque);
    create_info.setPresentMode(present_mode);
    create_info.setClipped(vk::True);

    if (old_swap_chain)
        create_info.setOldSwapchain(*old_swap_chain->swap_chain);

    swap_chain = ctx.device->createSwapchainKHRUnique(create_info);

    swap_chain_images = ctx.device->getSwapchainImagesKHR(*swap_chain);

    swap_chain_image_format = surface_format.format;
    swap_chain_extent = extent;
}

void SwapChain::create_image_views() {
    swap_chain_image_views.resize(swap_chain_images.size());
    for (std::size_t i = 0; i < swap_chain_images.size(); i++) {
        swap_chain_image_views[i] =
            ctx.device->createImageViewUnique(
                vk::ImageViewCreateInfo(
                    {},
                    swap_chain_images[i],
                    vk::ImageViewType::e2D,
                    swap_chain_image_format,
                    {},
                    vk::ImageSubresourceRange(
                        vk::ImageAspectFlagBits::eColor,
                        0,
                        1,
                        0,
                        1)));
    }
}

void SwapChain::create_render_pass() {
    vk::AttachmentDescription depth_attachment(
        {},
        find_depth_format(),
        vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eDontCare,
        vk::AttachmentLoadOp::eDontCare,
        vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eDepthStencilAttachmentOptimal);

    vk::AttachmentReference depth_attachment_ref(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

    vk::AttachmentDescription color_attachment(
        {},
        get_swap_chain_image_format(),
        vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare,
        vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::ePresentSrcKHR);

    vk::AttachmentReference color_attachment_ref(0, vk::ImageLayout::eColorAttachmentOptimal);

    vk::SubpassDescription subpass(
        {},
        vk::PipelineBindPoint::eGraphics,
        {},
        color_attachment_ref,
        {},
        &depth_attachment_ref,
        {});

    vk::SubpassDependency dependency(
        vk::SubpassExternal,
        0,
        vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        vk::AccessFlagBits::eNone,
        vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
        {});

    vk::AttachmentDescription attachments[] = { color_attachment, depth_attachment };

    render_pass = ctx.device->createRenderPassUnique(vk::RenderPassCreateInfo(
        {},
        attachments,
        subpass,
        dependency));
}

void SwapChain::create_framebuffers() {
    frame_buffers.resize(image_count());
    for (size_t i = 0; i < image_count(); i++) {
        vk::ImageView attachments[] = { *swap_chain_image_views[i], *depth_image_views[i] };
        frame_buffers[i] = ctx.device->createFramebufferUnique(vk::FramebufferCreateInfo(
            {},
            *render_pass,
            attachments,
            width(),
            height(),
            1));
    }
}

void SwapChain::create_depth_resources() {
    swap_chain_depth_format = find_depth_format();

    depth_images.resize(image_count());
    depth_image_memorys.resize(image_count());
    depth_image_views.resize(image_count());

    for (size_t i = 0; i < depth_images.size(); i++) {
        depth_images[i] = ctx.device->createImageUnique(vk::ImageCreateInfo(
            {},
            vk::ImageType::e2D,
            swap_chain_depth_format,
            vk::Extent3D(width(), height(), 1),
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::SharingMode::eExclusive,
            {},
            vk::ImageLayout::eUndefined));
        vk::MemoryRequirements mem_requirements = ctx.device->getImageMemoryRequirements(*depth_images[i]);
        depth_image_memorys[i] =
            ctx.device->allocateMemoryUnique(
                vk::MemoryAllocateInfo(
                    mem_requirements.size,
                    ctx.find_memory_type(mem_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)));
        ctx.device->bindImageMemory(*depth_images[i], *depth_image_memorys[i], 0);
        depth_image_views[i] = ctx.device->createImageViewUnique(vk::ImageViewCreateInfo(
            {},
            *depth_images[i],
            vk::ImageViewType::e2D,
            swap_chain_depth_format,
            {},
            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1)));
    }
}

void SwapChain::create_sync_objects() {
    in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);
    image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished_semaphores.resize(image_count());
    images_in_flight.resize(image_count());

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        image_available_semaphores[i] = ctx.device->createSemaphoreUnique(vk::SemaphoreCreateInfo());
        in_flight_fences[i] = ctx.device->createFenceUnique(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
    }
    for (size_t i = 0; i < image_count(); i++) {
        render_finished_semaphores[i] = ctx.device->createSemaphoreUnique(vk::SemaphoreCreateInfo());
        images_in_flight[i] = nullptr;
    }
}

vk::SurfaceFormatKHR SwapChain::choose_swap_surface_format(const std::vector<vk::SurfaceFormatKHR>& available_formats) {
    for (const auto& format : available_formats) {
        if (format.format == vk::Format::eB8G8R8A8Unorm &&
            format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return format;
        }
    }
    return available_formats[0];
}

vk::PresentModeKHR SwapChain::choose_swap_present_mode(const std::vector<vk::PresentModeKHR>& available_present_modes) {
    for (const auto& present_mode : available_present_modes) {
        if (present_mode == vk::PresentModeKHR::eMailbox) {
            return present_mode;
        }
    }

    // for (const auto& present_mode : available_present_modes) {
    //     if (present_mode == vk::PresentModeKHR::eImmediate) {
    //         return present_mode;
    //     }
    // }

    // V-Sync
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D SwapChain::choose_swap_extent(const vk::SurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        vk::Extent2D actual_extent = window_extent;
        actual_extent.width = std::max(
            capabilities.minImageExtent.width,
            std::min(capabilities.maxImageExtent.width, actual_extent.width));
        actual_extent.height = std::max(
            capabilities.minImageExtent.height,
            std::min(capabilities.maxImageExtent.height, actual_extent.height));
        return actual_extent;
    }
}

vk::Format SwapChain::find_depth_format() {
    constexpr auto features = vk::FormatFeatureFlagBits::eDepthStencilAttachment;
    for (const auto& format : { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint }) {
        auto properties = ctx.physical_device->getFormatProperties(format);
        if ((properties.optimalTilingFeatures & features) == features) {
            return format;
        }
        // if ((properties.linearTilingFeatures & features) == features) {
        //     return format;
        // }
    }
    throw std::runtime_error("failed to find supported format!");
}

}  // namespace aglea
