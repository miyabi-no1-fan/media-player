#version 450

layout(location = 0) out vec4 out_color;
layout(location = 0) in float color;

layout(set = 0, binding = 0, std140) uniform UBO {
    float progress;
};

void main() {
    if (color > progress) discard;
    out_color = vec4(255, 0, 0, 1);
}
