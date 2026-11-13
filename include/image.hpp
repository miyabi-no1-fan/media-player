/*
    media-player
    Copyright (C) 2026  miyabi-no1-fan

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include <vulkan/vulkan.hpp>

#include "context.hpp"
#include "vulkan/vulkan.hpp"

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
    vk::DescriptorImageInfo descriptor_info(vk::ImageLayout layout = {}, const vk::UniqueSampler& sampler = {});
    void invalidate(vk::DeviceSize size = vk::WholeSize, vk::DeviceSize offset = 0);

    const vk::UniqueImage& get_image() const { return image; }
    const vk::UniqueImageView& get_image_view() const { return view; }
    void* get_mapped_memory() const { return mapped; }

    vk::ImageType get_type() const { return type; }
    vk::Format get_format() const { return format; }
    vk::Extent3D get_extent() const { return extent; }
    vk::ImageUsageFlags get_usage_flags() const { return usage_flags; }
    vk::MemoryPropertyFlags get_memory_property_flags() const { return memory_property_flags; }

   private:
    const Context* ctx;
    void* mapped = nullptr;
    vk::UniqueImage image;
    vk::UniqueImageView view;
    vk::UniqueDeviceMemory memory;

    vk::ImageType type;
    vk::Format format;
    vk::Extent3D extent;
    vk::ImageUsageFlags usage_flags;
    vk::MemoryPropertyFlags memory_property_flags;
};

};  // namespace aglea