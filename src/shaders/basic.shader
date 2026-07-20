#shader vertex
#version 330 core

layout(location = %d) in vec4 position;

void main() {
    gl_Position = position;
}

#shader fragment
#version 330 core

layout(location = %d) out vec4 color;

void main() {
    color = vec4(0.0, 1.0, 0.0, 1.0);
}
