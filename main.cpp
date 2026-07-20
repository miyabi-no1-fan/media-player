#include <cassert>
#include <cstdio>
#include <fstream>
#include <ios>
#include <string>

#include "display.hpp"
#include "shader.hpp"
#include "slp_png/slp_image.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

void load_file(std::string& dst, const char* src_file) {
    assert(dst.size() == 0);
    std::ifstream file(src_file, std::ios::ate);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    dst.resize(size);
    file.read(dst.data(), size);
}

int main(int argc, const char* argv[]) {
    if (argc != 2) {
        std::printf("Unknown arguments");
        return -1;
    }

    Image image(argv[1]);
    Window window(image.width, image.height, argv[1]);

    GLuint vao;  // https://wikis.khronos.org/opengl/Vertex_Specification#Vertex_Array_Object
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    float pos[6] = {
        -0.5f, -0.5f,
        0.0f, 0.5f,
        0.5f, -0.5f};

    unsigned int buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(float), pos, GL_STATIC_DRAW);

    unsigned int position_attrib_index = 0;
    glEnableVertexAttribArray(position_attrib_index);
    glVertexAttribPointer(position_attrib_index, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, 0);

    Shader shader("basic", position_attrib_index);
    glUseProgram(shader.get_program());

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
