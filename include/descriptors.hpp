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
#include <memory>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "context.hpp"

namespace aglea {

class DescriptorSetLayout {
   public:
    class Builder {
       public:
        Builder(Context& ctx) : ctx{ ctx } {}

        Builder& add_binding(
            std::uint32_t binding,
            vk::DescriptorType descriptor_type,
            vk::ShaderStageFlags stage_flags,
            vk::Sampler* immutable_sampler = nullptr);
        std::unique_ptr<DescriptorSetLayout> build() const;

       private:
        const Context& ctx;
        std::unordered_map<std::uint32_t, vk::DescriptorSetLayoutBinding> bindings{};
    };

    DescriptorSetLayout() {}
    DescriptorSetLayout(const Context& ctx, const std::unordered_map<std::uint32_t, vk::DescriptorSetLayoutBinding>& bindings);
    DescriptorSetLayout(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout(DescriptorSetLayout&&) = default;
    DescriptorSetLayout& operator=(DescriptorSetLayout&&) = default;

    const vk::UniqueDescriptorSetLayout& get_descriptor_set_layout() const { return descriptor_set_layout; }

   private:
    vk::UniqueDescriptorSetLayout descriptor_set_layout;
    std::unordered_map<std::uint32_t, vk::DescriptorSetLayoutBinding> bindings;

    friend class DescriptorWriter;
};

class DescriptorPool {
   public:
    class Builder {
       public:
        Builder(const Context& ctx) : ctx{ ctx } {}

        Builder& add_pool_size(vk::DescriptorType descriptor_type, std::uint32_t count);
        Builder& set_pool_flags(vk::DescriptorPoolCreateFlags flags);
        Builder& set_max_sets(std::uint32_t count);
        std::unique_ptr<DescriptorPool> build() const;

       private:
        const Context& ctx;
        std::vector<vk::DescriptorPoolSize> pool_sizes{};
        std::uint32_t max_sets = 1000;
        vk::DescriptorPoolCreateFlags pool_flags = {};
    };

    DescriptorPool(
        const Context& ctx,
        std::uint32_t max_sets,
        vk::DescriptorPoolCreateFlags pool_flags,
        const std::vector<vk::DescriptorPoolSize>& pool_sizes);
    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;

    vk::UniqueDescriptorSet allocate_descriptor_set(const vk::UniqueDescriptorSetLayout& descriptor_set_layout) const;
    void free_descriptor_set(const std::vector<vk::DescriptorSet>& descriptors) const;
    void reset_pool();

   private:
    const Context& ctx;
    vk::UniqueDescriptorPool descriptor_pool;

    friend class DescriptorWriter;
};

class DescriptorWriter {
   public:
    DescriptorWriter(const Context& ctx, DescriptorSetLayout& set_layout, DescriptorPool& pool);

    DescriptorWriter& write_buffer(std::uint32_t binding, vk::DescriptorBufferInfo buffer_info, std::optional<vk::BufferView> buffer_view = {});
    DescriptorWriter& write_image(std::uint32_t binding, vk::DescriptorImageInfo image_info);

    vk::UniqueDescriptorSet build();
    void overwrite(const vk::UniqueDescriptorSet& set);

   private:
    const Context& ctx;
    DescriptorSetLayout& set_layout;
    DescriptorPool& pool;

    struct WriteInfo {
        std::uint32_t binding;
        std::variant<
            std::pair<vk::DescriptorBufferInfo, std::optional<vk::BufferView>>,
            vk::DescriptorImageInfo>
            descriptor_info;
    };
    std::vector<WriteInfo> write_infos;
};

};  // namespace aglea