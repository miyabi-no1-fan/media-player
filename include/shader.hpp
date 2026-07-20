#pragma once
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iosfwd>
#include <string>
#include <vector>

#include "utils.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#ifndef SHADER_HPP
#define SHADER_HPP

class Shader {
    unsigned int program;

    struct Parse {
        char shader_type[64];
        size_t start;
    };

   public:
    Shader(const char* name, unsigned int attrib_index) {
        char src_file[128] = {};
        std::sprintf(src_file, "/home/rei/Projects/media_player/src/shaders/%.*s.shader", 64, name);

        std::ifstream file(src_file, std::ios::ate);
        if (!file.is_open()) {
            panic("Load Shader Failed. File %s Not Found", src_file);
        }
        size_t size = std::streamoff(file.tellg());
        file.seekg(0, std::ios::beg);

        std::string content;
        content.resize(size);
        file.read(content.data(), size);

        std::string formated_content;
        formated_content.resize(size + 10 * 2);
        std::sprintf(formated_content.data(), content.c_str(), attrib_index, attrib_index);
        content.clear();
        size = std::strlen(formated_content.c_str());

        Parse p[2] = {};
        for (int i = 0; i < 2; i++) {
            size_t prev = 0;
            if (i > 0) {
                prev = i - 1;
            }
            p[i].start = formated_content.find("#shader", p[prev].start);
            if (std::sscanf(formated_content.c_str() + p[i].start, "#shader %63s", p[i].shader_type) != 1) {
                panic("Load Shader Failed. Bad #shader, at file: %s", src_file);
            }
            p[i].start += sizeof("#shader") + std::strlen(p[i].shader_type);
            LOGI("%lu %s", p[i].start, p[i].shader_type);
        }

        std::string vertex;
        std::string fragment;

        for (int i = 0; i < 2; i++) {
            std::string* dst;
            if (std::strcmp(p[i].shader_type, "vertex") == 0) {
                dst = &vertex;
            } else if (std::strcmp(p[i].shader_type, "fragment") == 0) {
                dst = &fragment;
            } else {
                panic("Load Shader Faild. Unkown shader type, at file %s", src_file);
            }

            size_t len;
            if (i + 1 < 2) {
                len = formated_content.find("#shader", p[i].start) - p[i].start;
            } else {
                len = size - p[i].start;
            }

            if (len == -1) {
                panic("idk");
            }

            (*dst).resize(len);
            std::memcpy((*dst).data(), formated_content.c_str() + p[i].start, len);
        }

        this->program = CreateShader(vertex.c_str(), fragment.c_str());
    }

    ~Shader() {
        glDeleteShader(this->program);
    }

    unsigned int get_program() const noexcept {
        return this->program;
    }

   private:
    static unsigned int CompileShader(unsigned int type, const char* src) noexcept {
        unsigned int id = glCreateShader(type);
        glShaderSource(id, 1, &src, NULL);
        glCompileShader(id);

        int result;
        glGetShaderiv(id, GL_COMPILE_STATUS, &result);
        if (result == GL_FALSE) {
            LOGE("Compile %s Shader Failed", (type == GL_VERTEX_SHADER) ? "GL_VERTEX_SHADER" : "GL_FRAGMENT_SHADER");

            int len;
            glGetShaderiv(id, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> msg(len);
            glGetShaderInfoLog(id, len, &len, msg.data());

            panic("Compile message:\n%*s", len, msg.data());
            // glDeleteShader(id);
            // return 0;
        }

        return id;
    }

    static unsigned int CreateShader(const char* vertex, const char* fragment) noexcept {
        unsigned int program = glCreateProgram();
        unsigned int vs = CompileShader(GL_VERTEX_SHADER, vertex);
        unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fragment);

        glAttachShader(program, vs);
        glAttachShader(program, fs);

        glLinkProgram(program);
        glValidateProgram(program);

        glDeleteShader(vs);
        glDeleteShader(fs);

        assert_gl();

        return program;
    }
};

#endif