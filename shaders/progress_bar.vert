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
