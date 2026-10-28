/*
    media-player
    Copyright (C) 2026  miyabi-no1-fan

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#version 450

layout(location = 0) out vec2 frag_color;

layout(set = 0, binding = 0, std140) uniform UBO {
    vec4 _transformation;
    bool is_color_range_jpeg;
    bool is_color_space_bt709;
};

const vec2 positions[4] = {
    vec2(-1, -1),
    vec2(1, -1),
    vec2(-1, 1),
    vec2(1, 1),
};

const vec2 colors[4] = {
    vec2(0, 0),
    vec2(1, 0),
    vec2(0, 1),
    vec2(1, 1),
};

void main() {
    mat2 transformation = transpose(mat2(_transformation));
    vec2 position = transformation * positions[gl_VertexIndex];
    gl_Position = vec4(position, 0.9, 1);
    frag_color = colors[gl_VertexIndex];
}
