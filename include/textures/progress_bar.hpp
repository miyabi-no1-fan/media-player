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
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "buffer.hpp"
#include "renderer.hpp"
#include "textures.hpp"

namespace aglea {

class ProgressBar : public Texture {
    Renderer& renderer;
    struct UBO {
        // current / duration
        float progress;
    };
    std::vector<std::unique_ptr<Buffer>> ubo;

   public:
    ProgressBar(Renderer& renderer) : renderer(renderer) {}
    void init() override;
    void update(float progress, std::uint32_t i);
    void draw(const vk::UniqueCommandBuffer& cmd, std::uint32_t i) override;

   private:
    void create_ubo();
    void create_descriptors();
    void create_pipeline();
};

}  // namespace aglea