#include <cstdio>

#include "display.hpp"
#include "slp_png/slp_image.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

int main(int argc, const char* argv[]) {
    if (argc != 2) {
        std::printf("Unknown arguments");
        return -1;
    }

    Image image(argv[1]);
    Window window(image.width, image.height, argv[1]);

    float pos[6] = {
        -0.5f, -0.5f,
        0.0f, 0.5f,
        0.5f, -0.5f};

    unsigned int buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(*pos), pos, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    while (!window.should_close()) {
        window.clear();
        // window.render(image.width, image.height, image.data());

        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        window.swap_buffers();
        window.poll_events();
    }

    return 0;
}
