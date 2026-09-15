#include "block_colors.hpp"

#include <cstdint>

namespace opencraft::client {

std::vector<glm::vec3> block_main_colors(const opencraft::client::AtlasImage &atlas, std::size_t registry_size) {
    std::vector<glm::vec3> colors(registry_size, glm::vec3(0.7f));
    const int tile_px = 16;
    for (std::size_t id = 0; id < registry_size; ++id) {
        const int tile = static_cast<int>(id) * 3 + 1; // side tile (mesher convention)
        const int tx = (tile % atlas.tiles_per_row) * tile_px;
        const int ty = (tile / atlas.tiles_per_row) * tile_px;
        glm::vec3 sum(0.0f);
        int count = 0;
        for (int y = 0; y < tile_px; ++y) {
            for (int x = 0; x < tile_px; ++x) {
                const std::uint32_t pixel = atlas.pixels[static_cast<std::size_t>((ty + y) * atlas.width + tx + x)];
                const float alpha = static_cast<float>((pixel >> 24) & 0xFF);
                if (alpha < 128.0f) {
                    continue;
                }
                sum += glm::vec3(static_cast<float>(pixel & 0xFF), static_cast<float>((pixel >> 8) & 0xFF),
                                 static_cast<float>((pixel >> 16) & 0xFF)) /
                       255.0f;
                ++count;
            }
        }
        if (count > 0) {
            colors[id] = sum / static_cast<float>(count);
        }
    }
    return colors;
}

} // namespace opencraft::client
