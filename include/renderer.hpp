#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "context.hpp"
#include "descriptors.hpp"
#include "swap_chain.hpp"
#include "textures.hpp"
#include "window.hpp"

namespace aglea {

class Renderer {
   public:
    Window& window;
    Context ctx;
    std::unique_ptr<SwapChain> swap_chain{};
    std::vector<std::pair<bool, vk::UniqueCommandBuffer>> command_buffers{};
    std::unique_ptr<DescriptorPool> descriptor_pool;

    // if textures change, call `invalidate_command_buffers`
    std::vector<aglea::Texture*> textures;

    Renderer(Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    std::optional<std::uint32_t> acquire_next_frame();
    void record_command_buffer(std::uint32_t i);
    void invalidate_command_buffers();
    void submit_frame(std::uint32_t i);

   private:
    void create_swap_chain();
    void create_command_buffers();
    void create_descriptor_pool();
};

}  // namespace aglea
