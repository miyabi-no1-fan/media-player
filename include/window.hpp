#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace aglea {

class Window {
    GLFWwindow* window;
    std::uint32_t width;
    std::uint32_t height;
    std::string name;

    std::chrono::nanoseconds frame_time;
    std::optional<std::chrono::steady_clock::time_point> next_frame{};

    bool frame_buffer_resized = false;

   public:
    Window(std::uint32_t width, std::uint32_t height, const std::string& name, double frame_time = 0.0);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    std::uint32_t get_width() const { return width; }
    std::uint32_t get_height() const { return height; }
    vk::Extent2D get_extent() const { return vk::Extent2D(width, height); }
    const std::string& get_name() const { return name; }
    double get_frame_time() const { return frame_time.count(); }

    bool should_close() const { return glfwWindowShouldClose(window) == GLFW_TRUE; }
    void poll_events();
    void wait_events() const { glfwWaitEvents(); }

    bool was_window_resized() const { return frame_buffer_resized; }
    void reset_window_resized_flag() { frame_buffer_resized = false; }

    vk::UniqueSurfaceKHR create_window_surface(const vk::UniqueInstance& instance) const;

   private:
    static void frame_buffer_resize_callback(GLFWwindow* window_, int width, int height);
};

}  // namespace aglea
