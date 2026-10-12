#include "image.hpp"

#include <cstddef>
#include <vulkan/vulkan.hpp>

namespace aglea {

static std::uint32_t find_memory_type(const Context& ctx, std::uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = ctx.physical_device->getMemoryProperties();
    for (std::uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

Image::Image(
    const Context& ctx,
    vk::ImageType type,
    vk::Format format,
    vk::Extent3D extent,
    vk::ImageUsageFlags usage_flags,
    vk::MemoryPropertyFlags memory_property_flags)
    : ctx(&ctx),
      type(type),
      format(format),
      layout(vk::ImageLayout::eUndefined),
      extent(extent),
      usage_flags(usage_flags),
      memory_property_flags(memory_property_flags) {
    image =
        ctx.device->createImageUnique(
            vk::ImageCreateInfo(
                {},
                type,
                format,
                extent,
                1,
                1,
                vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal,
                usage_flags,
                vk::SharingMode::eExclusive,
                ctx.graphics_family_index,
                vk::ImageLayout::eUndefined));
    vk::MemoryRequirements memRequirements = ctx.device->getImageMemoryRequirements(*image);
    memory =
        ctx.device->allocateMemoryUnique(
            vk::MemoryAllocateInfo(
                memRequirements.size,
                find_memory_type(ctx, memRequirements.memoryTypeBits, memory_property_flags)));
    ctx.device->bindImageMemory(*image, *memory, 0);

    view = ctx.device->createImageViewUnique(
        vk::ImageViewCreateInfo(
            {},
            *image,
            vk::ImageViewType(type),
            format,
            {},
            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)));
}

Image::~Image() {
    unmap();
}

void Image::map(vk::DeviceSize size, vk::DeviceSize offset) {
    mapped = static_cast<void*>(ctx->device->mapMemory(*memory, offset, size));
}

void Image::unmap() {
    if (mapped) {
        ctx->device->unmapMemory(*memory);
        mapped = nullptr;
    }
}

void Image::write_to_image(void* data, vk::DeviceSize size, vk::DeviceSize offset) {
    memcpy(static_cast<std::byte*>(mapped) + offset, data, size);
}

/**
 * Flush a memory range of the image to make it visible to the device
 *
 * @note Only required for non-coherent memory
 */
void Image::flush(vk::DeviceSize size, vk::DeviceSize offset) {
    ctx->device->flushMappedMemoryRanges(
        vk::MappedMemoryRange(*memory, offset, size));
}

/**
 * Invalidate a memory range of the image to make it visible to the host
 *
 * @note Only required for non-coherent memory
 */
void Image::invalidate(vk::DeviceSize size, vk::DeviceSize offset) {
    ctx->device->invalidateMappedMemoryRanges(
        vk::MappedMemoryRange(*memory, offset, size));
}

/**
 * Create a image info descriptor
 */
vk::DescriptorImageInfo Image::descriptor_info(const vk::UniqueSampler& sampler) {
    return vk::DescriptorImageInfo(
        *sampler,
        *view,
        layout);
}

};  // namespace aglea