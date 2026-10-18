#version 450

layout(location = 0) in vec2 color;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0, std140) uniform UBO {
    vec4 _transformation;
    bool is_color_range_jpeg;
    bool is_color_space_bt709;
};
layout(set = 0, binding = 1) uniform sampler2D plane0;
layout(set = 0, binding = 2) uniform sampler2D plane1;
layout(set = 0, binding = 3) uniform sampler2D plane2;

void main() {
    const float y_offset = is_color_range_jpeg ? 0.0f : 16.0f / 255.f;
    const float y_scale = is_color_range_jpeg ? 1.0f : 255.f / 219.0f;
    const float c_scale = is_color_range_jpeg ? 1.0f : 255.f / 224.0f;
    const mat3 M = transpose(
        is_color_space_bt709
            ? mat3(vec3(1.0f, 0.0f, 1.5748f), vec3(1.0f, -0.187324f, -0.468124f), vec3(1.0f, 1.8556f, 0.0f))  // BT.709
            : mat3(vec3(1.0f, 0.0f, 1.402f), vec3(1.0f, -0.344136f, -0.714136f), vec3(1.0f, 1.772f, 0.0f)));  // BT.601
    float Y = texture(plane0, color).r;
    float U = texture(plane1, color).r;
    float V = texture(plane2, color).r;
    Y = (Y - y_offset) * y_scale;
    U = (U - 128.f / 255.f) * c_scale;
    V = (V - 128.f / 255.f) * c_scale;
    vec3 rgb = M * vec3(Y, U, V);
    rgb = clamp(rgb, vec3(0.f), vec3(1.f));
    out_color = vec4(rgb, 1.f);
}
