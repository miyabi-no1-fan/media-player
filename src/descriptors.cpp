#include "descriptors.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace aglea {

// *************** Descriptor Set Layout Builder *********************

DescriptorSetLayout::Builder& DescriptorSetLayout::Builder::add_binding(
    std::uint32_t binding,
    vk::DescriptorType descriptor_type,
    vk::ShaderStageFlags stage_flags,
    vk::Sampler* immutableSampler) {
    assert(bindings.count(binding) == 0 && "Binding already in use");
    bindings[binding] =
        vk::DescriptorSetLayoutBinding(
            binding,
            descriptor_type,
            1,
            stage_flags,
            immutableSampler);
    return *this;
}

std::unique_ptr<DescriptorSetLayout> DescriptorSetLayout::Builder::build() const {
    return std::make_unique<DescriptorSetLayout>(ctx, bindings);
}

// *************** Descriptor Set Layout *********************

DescriptorSetLayout::DescriptorSetLayout(const Context& ctx, const std::unordered_map<std::uint32_t, vk::DescriptorSetLayoutBinding>& bindings)
    : bindings{ bindings } {
    std::vector<vk::DescriptorSetLayoutBinding> set_layout_bindings{};
    set_layout_bindings.reserve(bindings.bucket_count());
    for (auto&& kv : bindings) set_layout_bindings.push_back(kv.second);
    descriptor_set_layout =
        ctx.device->createDescriptorSetLayoutUnique(
            vk::DescriptorSetLayoutCreateInfo({}, set_layout_bindings));
}

// *************** Descriptor Pool Builder *********************

DescriptorPool::Builder& DescriptorPool::Builder::add_pool_size(
    vk::DescriptorType descriptor_type, std::uint32_t count) {
    pool_sizes.push_back({ descriptor_type, count });
    return *this;
}

DescriptorPool::Builder& DescriptorPool::Builder::set_pool_flags(
    vk::DescriptorPoolCreateFlags flags) {
    pool_flags = flags;
    return *this;
}
DescriptorPool::Builder& DescriptorPool::Builder::set_max_sets(std::uint32_t count) {
    max_sets = count;
    return *this;
}

std::unique_ptr<DescriptorPool> DescriptorPool::Builder::build() const {
    return std::make_unique<DescriptorPool>(ctx, max_sets, pool_flags, pool_sizes);
}

// *************** Descriptor Pool *********************

DescriptorPool::DescriptorPool(
    const Context& ctx,
    std::uint32_t max_sets,
    vk::DescriptorPoolCreateFlags pool_flags,
    const std::vector<vk::DescriptorPoolSize>& pool_sizes)
    : ctx{ ctx } {
    descriptor_pool =
        ctx.device->createDescriptorPoolUnique(
            vk::DescriptorPoolCreateInfo(
                pool_flags | vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                max_sets,
                static_cast<std::uint32_t>(pool_sizes.size()),
                pool_sizes.data()));
}

vk::UniqueDescriptorSet
DescriptorPool::allocate_descriptor_set(const vk::UniqueDescriptorSetLayout& descriptor_set_layout) const {
    return std::move(ctx.device->allocateDescriptorSetsUnique(
        vk::DescriptorSetAllocateInfo(
            *descriptor_pool,
            *descriptor_set_layout))[0]);
}

void DescriptorPool::free_descriptor_set(const std::vector<vk::DescriptorSet>& descriptors) const {
    ctx.device->freeDescriptorSets(*descriptor_pool, descriptors);
}

void DescriptorPool::reset_pool() {
    ctx.device->resetDescriptorPool(*descriptor_pool);
}

// *************** Descriptor Writer *********************

DescriptorWriter::DescriptorWriter(const Context& ctx, DescriptorSetLayout& set_layout, DescriptorPool& pool)
    : ctx(ctx), set_layout{ set_layout }, pool{ pool } {}

DescriptorWriter& DescriptorWriter::write_buffer(
    std::uint32_t binding, vk::DescriptorBufferInfo buffer_info, std::optional<vk::BufferView> buffer_view) {
    assert(set_layout.bindings.count(binding) == 1 && "Layout does not contain specified binding");
    assert(set_layout.bindings[binding].descriptorCount == 1 &&
           "Binding single descriptor info, but binding expects multiple");
    write_infos.push_back(WriteInfo{
        binding,
        std::pair(buffer_info, buffer_view),
    });
    return *this;
}

DescriptorWriter& DescriptorWriter::write_image(
    std::uint32_t binding, vk::DescriptorImageInfo image_info) {
    assert(set_layout.bindings.count(binding) == 1 && "Layout does not contain specified binding");
    assert(set_layout.bindings[binding].descriptorCount == 1 &&
           "Binding single descriptor info, but binding expects multiple");
    write_infos.push_back(WriteInfo{
        binding,
        image_info,
    });
    return *this;
}

vk::UniqueDescriptorSet DescriptorWriter::build() {
    vk::UniqueDescriptorSet set = pool.allocate_descriptor_set(set_layout.get_descriptor_set_layout());
    overwrite(set);
    return set;
}

void DescriptorWriter::overwrite(const vk::UniqueDescriptorSet& set) {
    std::vector<vk::WriteDescriptorSet> writes{};
    for (auto&& info : write_infos) {
        if (auto* bufferInfo = std::get_if<0>(&info.descriptor_info)) {
            vk::BufferView* buffer_view = (bufferInfo->second) ? (&*bufferInfo->second) : nullptr;
            writes.push_back(vk::WriteDescriptorSet(
                *set,
                info.binding,
                {},
                1,
                set_layout.bindings[info.binding].descriptorType,
                nullptr,
                &bufferInfo->first,
                buffer_view));
        } else if (auto* imageInfo = std::get_if<1>(&info.descriptor_info)) {
            writes.push_back(vk::WriteDescriptorSet(
                *set,
                info.binding,
                {},
                1,
                set_layout.bindings[info.binding].descriptorType,
                imageInfo,
                nullptr,
                nullptr));
        }
    }
    ctx.device->updateDescriptorSets(writes, {});
}

};  // namespace aglea