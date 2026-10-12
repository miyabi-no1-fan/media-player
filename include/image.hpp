#pragma once
#include <vulkan/vulkan.hpp>

#include "buffer.hpp"
#include "context.hpp"

namespace aglea {

class Image {
   public:
    Image(
        const Context& ctx,
        vk::ImageType type,
        vk::Format format,
        vk::Extent3D extent,
        vk::ImageUsageFlags usage_flags,
        vk::MemoryPropertyFlags memory_property_flags);
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    Image(Image&&) noexcept = default;
    Image& operator=(Image&&) = default;

    void map(vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);
    void unmap();

    void write_to_image(void* data, vk::DeviceSize size, vk::DeviceSize offset);
    void flush(vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);
    vk::DescriptorImageInfo descriptor_info(const vk::UniqueSampler& sampler = {});
    void invalidate(vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);

    const vk::UniqueImage& get_image() const { return image; }
    const vk::UniqueImageView& get_image_view() const { return view; }
    void* get_mapped_memory() const { return mapped; }

    vk::ImageType get_type() const { return type; }
    vk::Format get_format() const { return format; }
    vk::Extent3D get_extent() const { return extent; }
    vk::ImageLayout get_layout() const { return layout; }
    vk::ImageUsageFlags get_usage_flags() const { return usage_flags; }
    vk::MemoryPropertyFlags get_memory_property_flags() const { return memory_property_flags; }

    void transition_image_layout(
        const vk::UniqueCommandBuffer& cmd,
        vk::ImageLayout new_layout,
        vk::AccessFlags src_access, vk::AccessFlags dst_access,
        vk::PipelineStageFlags src_stage, vk::PipelineStageFlags dst_stage) {
        vk::ImageMemoryBarrier barrier(
            src_access,
            dst_access,
            layout,
            new_layout,
            ctx->graphics_family_index,
            ctx->graphics_family_index,
            *image,
            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
        cmd->pipelineBarrier(src_stage, dst_stage, {}, nullptr, nullptr, barrier);
        layout = new_layout;
    }

   private:
    const Context* ctx;
    void* mapped = nullptr;
    vk::UniqueImage image;
    vk::UniqueImageView view;
    vk::UniqueDeviceMemory memory;

    vk::ImageType type;
    vk::Format format;
    vk::ImageLayout layout;
    vk::Extent3D extent;
    vk::ImageUsageFlags usage_flags;
    vk::MemoryPropertyFlags memory_property_flags;
};

// F(void*), size is in bytes
template <typename F>
static Image staging_buffer(
    const Context& ctx,
    vk::DeviceSize size,
    vk::ImageType type,
    vk::Format format,
    vk::Extent3D extent,
    vk::ImageUsageFlags usage,
    vk::MemoryPropertyFlags properties,
    F&& f) {
    Buffer staged(
        ctx,
        1,
        size,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    staged.map();
    f(staged.get_mapped_memory());
    staged.unmap();
    Image image(
        ctx,
        type,
        format,
        extent,
        usage | vk::ImageUsageFlagBits::eTransferDst,
        properties);
    ctx.single_time_commands([&staged, &image](vk::UniqueCommandBuffer& cmd) {
        // clang-format off
        vk::BufferImageCopy region(
            0, 0, 0,
            vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
            { 0, 0, 0 },
            image.get_extent()
        );
        // clang-format on
        image.transition_image_layout(
            cmd,
            vk::ImageLayout::eTransferDstOptimal,
            {},
            vk::AccessFlagBits::eTransferWrite,
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer);
        cmd->copyBufferToImage(*staged.get_buffer(), *image.get_image(), vk::ImageLayout::eTransferDstOptimal, region);
    });
    return image;
}

};  // namespace aglea