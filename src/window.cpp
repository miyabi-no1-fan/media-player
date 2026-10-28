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
#include "window.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>
#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace aglea {

using namespace std::chrono;

Window::Window(std::uint32_t width, std::uint32_t height, const std::string& name, double frame_time)
    : width(width),
      height(height),
      name(name),
      frame_time(duration_cast<nanoseconds>(duration<double>(frame_time))) {
    if (glfwInit() != GLFW_TRUE)
        throw std::runtime_error("Failed to initialize window.");
    if (glfwVulkanSupported() != GLFW_TRUE)
        throw std::runtime_error("GLFW does not support Vulkan.");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(width, height, name.c_str(), nullptr, nullptr);
    if (window == nullptr)
        throw std::runtime_error("Failed to create window.");

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, frame_buffer_resize_callback);
}

Window::~Window() {
    glfwDestroyWindow(window);
    glfwTerminate();
}

void Window::poll_events() {
    if (next_frame) {
        std::this_thread::sleep_until(*next_frame);
        *next_frame += frame_time;

        auto now = steady_clock::now();  // resync if we missed a frame
        if (now > *next_frame) next_frame = now + frame_time;
    } else {
        next_frame = steady_clock::now() + frame_time;
    }
    return glfwPollEvents();
}

vk::UniqueSurfaceKHR Window::create_window_surface(const vk::UniqueInstance& instance) const {
    VkSurfaceKHR surface;
    VkResult res = glfwCreateWindowSurface(*instance, window, nullptr, &surface);
    if (res != VK_SUCCESS)
        throw std::runtime_error("Failed to create window surface.");
    return vk::UniqueSurfaceKHR(surface, *instance);
}

void Window::frame_buffer_resize_callback(GLFWwindow* window_, int width, int height) {
    Window* window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window_));
    window->frame_buffer_resized = true;
    window->width = static_cast<uint32_t>(width);
    window->height = static_cast<uint32_t>(height);
}

}  // namespace aglea
