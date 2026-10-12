#include "buffer.hpp"

#include <cstring>
#include <vulkan/vulkan.hpp>

namespace aglea {

vk::DeviceSize Buffer::get_alignment(vk::DeviceSize instance_size, vk::DeviceSize min_offset_alignment) {
    if (min_offset_alignment > 0) {
        return (instance_size + min_offset_alignment - 1) & ~(min_offset_alignment - 1);
    }
    return instance_size;
}

Buffer::Buffer(
    const Context& ctx,
    vk::DeviceSize instance_size,
    std::uint32_t instance_count,
    vk::BufferUsageFlags usage_flags,
    vk::MemoryPropertyFlags memory_property_flags,
    vk::DeviceSize min_offset_alignment)
    : ctx{ &ctx },
      instance_count{ instance_count },
      instance_size{ instance_size },
      usage_flags{ usage_flags },
      memory_property_flags{ memory_property_flags } {
    alignment_size = get_alignment(instance_size, min_offset_alignment);
    buffer_size = alignment_size * instance_count;
    buffer =
        ctx.device->createBufferUnique(
            vk::BufferCreateInfo(
                {},
                buffer_size,
                usage_flags,
                vk::SharingMode::eExclusive,
                1,
                &ctx.graphics_family_index));
    vk::MemoryRequirements memRequirements = ctx.device->getBufferMemoryRequirements(*buffer);
    memory =
        ctx.device->allocateMemoryUnique(
            vk::MemoryAllocateInfo(
                memRequirements.size,
                ctx.find_memory_type(memRequirements.memoryTypeBits, memory_property_flags)));
    ctx.device->bindBufferMemory(*buffer, *memory, 0);
}

Buffer::~Buffer() {
    unmap();
}

/**
 * Map a memory range of this buffer. If successful, mapped points to the specified buffer range.
 *
 * @param size (Optional) Size of the memory range to map. Pass vk::WholeSize to map the complete
 * buffer range.
 * @param offset (Optional) Byte offset from beginning
 */
void Buffer::map(vk::DeviceSize size, vk::DeviceSize offset) {
    mapped = static_cast<void*>(ctx->device->mapMemory(*memory, offset, size));
}

void Buffer::unmap() {
    if (mapped) {
        ctx->device->unmapMemory(*memory);
        mapped = nullptr;
    }
}

void Buffer::write_to_buffer(void* data, vk::DeviceSize size, vk::DeviceSize offset) {
    if (size == vk::WholeSize) {
        memcpy(mapped, data, buffer_size);
    } else {
        char* memOffset = (char*)mapped;
        memOffset += offset;
        memcpy(memOffset, data, size);
    }
}

/**
 * Flush a memory range of the buffer to make it visible to the device
 *
 * @note Only required for non-coherent memory
 */
void Buffer::flush(vk::DeviceSize size, vk::DeviceSize offset) {
    ctx->device->flushMappedMemoryRanges(
        vk::MappedMemoryRange(*memory, offset, size));
}

/**
 * Invalidate a memory range of the buffer to make it visible to the host
 *
 * @note Only required for non-coherent memory
 */
void Buffer::invalidate(vk::DeviceSize size, vk::DeviceSize offset) {
    ctx->device->invalidateMappedMemoryRanges(
        vk::MappedMemoryRange(*memory, offset, size));
}

/**
 * Create a buffer info descriptor
 */
vk::DescriptorBufferInfo Buffer::descriptor_info(vk::DeviceSize size, vk::DeviceSize offset) {
    return vk::DescriptorBufferInfo(
        *buffer,
        offset,
        size);
}

/**
 * Copies "instanceSize" bytes of data to the mapped buffer at an offset of index * alignmentSize
 */
void Buffer::write_to_index(void* data, int index) {
    write_to_buffer(data, instance_size, index * alignment_size);
}

/**
 *  Flush the memory range at index * alignmentSize of the buffer to make it visible to the device
 */
void Buffer::flush_index(int index) { return flush(alignment_size, index * alignment_size); }

/**
 * Create a buffer info descriptor
 */
vk::DescriptorBufferInfo Buffer::index_descriptor_info(int index) {
    return descriptor_info(alignment_size, index * alignment_size);
}

/**
 * Invalidate a memory range of the buffer to make it visible to the host
 *
 * @note Only required for non-coherent memory
 */
void Buffer::invalidate_index(int index) {
    return invalidate(alignment_size, index * alignment_size);
}

};  // namespace aglea