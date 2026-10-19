#version 450

layout(location = 0) out float frag_color;

layout(set = 0, binding = 0, std140) uniform UBO {
    float progress;
};

const vec2 positions[4] = {
    vec2(-1, 0.99),
    vec2(1, 0.99),
    vec2(-1, 1),
    vec2(1, 1),
};
const float colors[4] = { 0, 1, 0, 1 };

void main() {
    vec2 position = positions[gl_VertexIndex];
    gl_Position = vec4(position, 0, 1);
    frag_color = colors[gl_VertexIndex];
}
