#include "display.hpp"

#include <GL/glext.h>

#include <cstdio>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <cstddef>
#include <cstdlib>

#include "utils.hpp"

#define assert_gl()                                         \
    {                                                       \
        GLenum err = glGetError();                          \
        if (err != GL_NO_ERROR) {                           \
            switch (err) {                                  \
                case GL_INVALID_ENUM:                       \
                    panic("INVALID_ENUM");                  \
                case GL_INVALID_VALUE:                      \
                    panic("INVALID_VALUE");                 \
                case GL_INVALID_OPERATION:                  \
                    panic("INVALID_OPERATION");             \
                case GL_OUT_OF_MEMORY:                      \
                    panic("OUT_OF_MEMORY");                 \
                case GL_INVALID_FRAMEBUFFER_OPERATION:      \
                    panic("INVALID_FRAMEBUFFER_OPERATION"); \
                default:                                    \
                    panic("UNKNOWN ERROR");                 \
            }                                               \
        }                                                   \
    }

Window::Window(int width, int height, const char* title) {
    assert(this->window == NULL);

    glfwSetErrorCallback(this->error_callback);
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
    if (!glfwInit()) {
        throw this->Error::InitErr;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    GLFWwindow* window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (window == NULL) {
        glfwTerminate();
        throw this->Error::InitErr;
    }

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, this->key_callback);  // or glfwSetCharCallback
    glfwSwapInterval(1);

    if (gladLoadGL(glfwGetProcAddress) == 0) {
        glfwTerminate();
        throw this->Error::InitErr;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenTextures(1, &this->rendererID);
    glBindTexture(GL_TEXTURE_2D, this->rendererID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    std::printf("%s\n", glGetString(GL_VERSION));

    assert_gl();

    this->window = window;
}

void Window::render(size_t width, size_t height, void* pixels) {
    assert(pixels != NULL);
    assert(width != 0);
    assert(height != 0);

    // glBindTexture(GL_TEXTURE_2D, this->rendererID);
    // glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);

    assert_gl();
}

void Window::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    /*if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }*/
}

void Window::error_callback(int error, const char* description) {
    panic("Display Error: %s", description);
}

Window::~Window() {
    glDeleteTextures(1, &this->rendererID);
    glfwDestroyWindow(this->window);
    this->window = NULL;
    glfwTerminate();
}
