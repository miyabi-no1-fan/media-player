#version 450

layout(location = 0) out vec2 frag_color;

layout(set = 0, binding = 0, std140) uniform UBO {
    vec4 _transformation;
};

vec2 positions[4] = {
    vec2(-1, -1),
    vec2(1, -1),
    vec2(-1, 1),
    vec2(1, 1),
};

vec2 colors[4] = {
    vec2(0, 0),
    vec2(1, 0),
    vec2(0, 1),
    vec2(1, 1),
};

void main() {
    mat2 transformation = transpose(mat2(_transformation));
    vec2 position = transformation * positions[gl_VertexIndex];
    gl_Position = vec4(position, 0, 1);
    frag_color = colors[gl_VertexIndex];
}
