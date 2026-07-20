#ifndef UTILS_HPP
#define UTILS_HPP
#include <cassert>
#include <cstdio>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#define LOGI(...)                          \
    do {                                   \
        std::fprintf(stdout, "INFO: ");    \
        std::fprintf(stdout, __VA_ARGS__); \
        std::fprintf(stdout, "\n");        \
        std::fflush(stderr);               \
    } while (0)

#define LOGE(...)                          \
    do {                                   \
        std::fprintf(stderr, "ERROR: ");   \
        std::fprintf(stderr, __VA_ARGS__); \
        std::fprintf(stderr, "\n");        \
        std::fflush(stderr);               \
    } while (0)

#define panic(...)         \
    do {                   \
        LOGE(__VA_ARGS__); \
        assert(false);     \
    } while (0)

#define assert_gl()                                         \
    do {                                                    \
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
    } while (0)

#endif