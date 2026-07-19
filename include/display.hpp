#ifndef DISPLAY_HPP
#define DISPLAY_HPP

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

class Window {
   private:
    GLFWwindow* window = NULL;
    GLuint rendererID = 0;
    unsigned int buffer;

   public:
    enum Error {
        InitErr
    };

    Window(int width, int height, const char* title);
    ~Window();

    void render(size_t width, size_t height, void* pixels);

    bool should_close() { return glfwWindowShouldClose(this->window) != 0; }
    void clear() { glClear(GL_COLOR_BUFFER_BIT); }
    void swap_buffers() { glfwSwapBuffers(this->window); }
    void poll_events() { glfwPollEvents(); }
    void wait_events() { glfwWaitEvents(); }

    static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void error_callback(int error, const char* description);
};

#endif