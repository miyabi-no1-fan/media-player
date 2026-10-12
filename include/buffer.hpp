#pragma once
#include <cstdint>
#include <vulkan/vulkan.hpp>

#include "context.hpp"

namespace aglea {

class Buffer {
   public:
    Buffer(
        const Context& ctx,
        vk::DeviceSize instance_size,
        std::uint32_t instance_count,
        vk::BufferUsageFlags usage_flags,
        vk::MemoryPropertyFlags memory_property_flags,
        vk::DeviceSize min_offset_alignment = 1);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&&) noexcept = default;
    Buffer& operator=(Buffer&&) = default;

    void map(vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);
    void unmap();

    void write_to_buffer(void* data, vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);
    void flush(vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);
    vk::DescriptorBufferInfo descriptor_info(vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);
    void invalidate(vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);

    void write_to_index(void* data, int index);
    void flush_index(int index);
    vk::DescriptorBufferInfo index_descriptor_info(int index);
    void invalidate_index(int index);

    const vk::UniqueBuffer& get_buffer() const { return buffer; }
    void* get_mapped_memory() const { return mapped; }
    std::uint32_t get_instance_count() const { return instance_count; }
    vk::DeviceSize get_instance_size() const { return instance_size; }
    vk::DeviceSize get_alignment_size() const { return instance_size; }
    vk::BufferUsageFlags get_usage_flags() const { return usage_flags; }
    vk::MemoryPropertyFlags get_memory_property_flags() const { return memory_property_flags; }
    vk::DeviceSize get_buffer_size() const { return buffer_size; }

   private:
    static vk::DeviceSize get_alignment(vk::DeviceSize instance_size, vk::DeviceSize min_offset_alignment);

    const Context* ctx;
    void* mapped = nullptr;
    vk::UniqueBuffer buffer;
    vk::UniqueDeviceMemory memory;

    vk::DeviceSize buffer_size;
    std::uint32_t instance_count;
    vk::DeviceSize instance_size;
    vk::DeviceSize alignment_size;
    vk::BufferUsageFlags usage_flags;
    vk::MemoryPropertyFlags memory_property_flags;
};

// F(void*)
template <typename F>
static Buffer staging_buffer(
    const Context& ctx,
    vk::DeviceSize instance_size,
    std::uint32_t instance_count,
    vk::BufferUsageFlags usage,
    vk::MemoryPropertyFlags properties,
    F&& f) {
    Buffer staged(
        ctx,
        instance_size,
        instance_count,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    staged.map();
    f(staged.get_mapped_memory());
    staged.unmap();
    Buffer buffer(
        ctx,
        instance_size,
        instance_count,
        usage | vk::BufferUsageFlagBits::eTransferDst,
        properties);
    ctx.single_time_commands([&staged, &buffer](vk::UniqueCommandBuffer& cmd) {
        cmd->copyBuffer(*staged.get_buffer(), *buffer.get_buffer(), vk::BufferCopy(0, 0, buffer.get_buffer_size()));
    });
    return buffer;
}

};  // namespace aglea