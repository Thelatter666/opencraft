#include "atlas.hpp"

#include "opencraft/render/mesher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace opencraft::client {

namespace {

constexpr int kTileSize = 16;

std::uint32_t rgba(int r, int g, int b, int a = 255) {
    return (static_cast<std::uint32_t>(a & 0xFF) << 24) | (static_cast<std::uint32_t>(b & 0xFF) << 16) |
           (static_cast<std::uint32_t>(g & 0xFF) << 8) | static_cast<std::uint32_t>(r & 0xFF);
}

struct Rgb {
    int r;
    int g;
    int b;
};

std::uint32_t shifted(const Rgb &c, int delta, int alpha = 255) {
    return rgba(std::clamp(c.r + delta, 0, 255), std::clamp(c.g + delta, 0, 255), std::clamp(c.b + delta, 0, 255),
                alpha);
}

std::uint32_t hash2(int x, int y, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393U + static_cast<std::uint32_t>(y) * 668265263U ^ seed;
    h = (h ^ (h >> 13)) * 1274126177U;
    return h ^ (h >> 16);
}

// Low-saturation base palette, one entry per launch block (abstract colors,
// deliberately not modeled on any existing game's texture art).
Rgb base_color(const std::string &id) {
    if (id == "dirt")
        return {100, 76, 60};
    if (id == "grass_block")
        return {96, 146, 78};
    if (id == "stone")
        return {128, 128, 133};
    if (id == "cobblestone")
        return {112, 112, 118};
    if (id == "sand")
        return {206, 190, 148};
    if (id == "gravel")
        return {138, 133, 128};
    if (id == "sandstone")
        return {200, 185, 145};
    if (id == "log")
        return {110, 86, 60};
    if (id == "leaves")
        return {70, 112, 60};
    if (id == "planks")
        return {168, 132, 92};
    if (id == "glass")
        return {172, 200, 214};
    if (id == "water")
        return {56, 110, 170};
    if (id == "bedrock")
        return {70, 70, 74};
    if (id == "coal_ore")
        return {128, 128, 133};
    if (id == "copper_ore")
        return {128, 128, 133};
    if (id == "iron_ore")
        return {128, 128, 133};
    if (id == "gold_ore")
        return {128, 128, 133};
    if (id == "diamond_ore")
        return {128, 128, 133};
    if (id == "snow_block")
        return {226, 230, 236};
    if (id == "obsidian")
        return {44, 36, 58};
    return {150, 150, 150};
}

Rgb ore_speck_color(const std::string &id) {
    if (id == "coal_ore")
        return {40, 40, 44};
    if (id == "copper_ore")
        return {182, 118, 76};
    if (id == "iron_ore")
        return {200, 168, 152};
    if (id == "gold_ore")
        return {216, 182, 104};
    if (id == "diamond_ore")
        return {116, 198, 204};
    return {0, 0, 0};
}

bool is_ore(const std::string &id) {
    return id == "coal_ore" || id == "copper_ore" || id == "iron_ore" || id == "gold_ore" || id == "diamond_ore";
}

// Paints one 16x16 tile into the atlas. `tx0/ty0` are the tile's pixel origin
// in atlas space; `py` grows with texture v (bottom-up), so the visual top of
// a tile sits at py == 15.
void paint_tile(AtlasImage &atlas, int tx0, int ty0, std::uint16_t block_id, int slot, const std::string &id) {
    const Rgb base = base_color(id);
    const std::uint32_t seed = static_cast<std::uint32_t>(block_id) * 2654435761U + static_cast<std::uint32_t>(slot);

    for (int py = 0; py < kTileSize; ++py) {
        for (int px = 0; px < kTileSize; ++px) {
            std::uint32_t color = 0;
            const int v_from_top = 15 - py; // 0 at visual top of the tile

            if (id == "water") {
                // Translucent with gentle wave stripes.
                const bool wave = ((py + px / 3) % 8) < 2;
                color = shifted(base, wave ? 16 : 0, 168);
            } else if (id == "glass") {
                const bool frame = px == 0 || px == 15 || py == 0 || py == 15;
                color = frame ? shifted({150, 182, 198}, 0, 190) : shifted(base, 0, 42);
            } else if (id == "leaves") {
                const Rgb tone = hash2(px, py, seed) % 5 < 2 ? Rgb{54, 92, 48} : base;
                color = shifted(tone, static_cast<int>(hash2(px, py, seed + 1) % 7) - 3);
            } else if (id == "grass_block" && slot == 1 && v_from_top < 5) {
                // Jagged green fringe over dirt on grass side faces.
                const int fringe = 3 + static_cast<int>(hash2(px, 7, seed) % 3);
                color = v_from_top < fringe ? shifted({88, 138, 72}, static_cast<int>(hash2(px, py, seed) % 9) - 4)
                                            : shifted({100, 76, 60}, static_cast<int>(hash2(px, py, seed) % 9) - 4);
            } else if (id == "log" && slot != 0) {
                // Vertical bark stripes.
                const bool stripe = (px % 4) < 2;
                color =
                    shifted(stripe ? Rgb{96, 74, 52} : Rgb{122, 98, 70}, static_cast<int>(hash2(px, py, seed) % 7) - 3);
            } else if (id == "log") {
                // Growth rings on top/bottom.
                const int ring = std::max(std::abs(px - 7), std::abs(py - 7)) % 3;
                color = shifted(ring == 0 ? Rgb{140, 112, 78} : Rgb{110, 86, 60},
                                static_cast<int>(hash2(px, py, seed) % 5) - 2);
            } else if (id == "planks") {
                const bool seam = (py % 4) == 0 || px == ((py / 4) % 2 == 0 ? 4 : 11);
                color = shifted(base, seam ? -36 : static_cast<int>(hash2(px, py, seed) % 9) - 4);
            } else if (id == "cobblestone") {
                const int cell = static_cast<int>(hash2(px / 4, py / 4, seed) % 25) - 12;
                const bool edge = (px % 4) == 0 || (py % 4) == 0;
                color = shifted(base, cell - (edge ? 16 : 0));
            } else if (id == "bedrock") {
                color = hash2(px, py, seed) % 2 != 0 ? shifted({54, 54, 58}, 0) : shifted({88, 88, 94}, 0);
            } else if (is_ore(id)) {
                // Stone body with 2x2 ore specks.
                const bool speck = hash2(px / 2, py / 2, seed) % 9 < 2;
                color = speck ? shifted(ore_speck_color(id), static_cast<int>(hash2(px, py, seed) % 9) - 4)
                              : shifted(base, static_cast<int>(hash2(px, py, seed) % 9) - 4);
            } else if (id == "obsidian") {
                const bool glint = hash2(px, py, seed) % 13 == 0;
                color = shifted(glint ? Rgb{74, 62, 96} : base, static_cast<int>(hash2(px, py, seed + 2) % 5) - 2);
            } else if (id == "sandstone" && slot == 1) {
                const bool band = (py / 4) % 2 == 0;
                color = shifted(base, band ? 0 : -10);
            } else {
                // Generic per-slot motif: top = fine checker, side = strata,
                // bottom = crosshatch; plus per-pixel grain everywhere.
                int delta = static_cast<int>(hash2(px, py, seed) % 9) - 4;
                if (slot == 0) {
                    delta += ((px / 2 + py / 2) % 2 == 0) ? 5 : -5;
                } else if (slot == 1) {
                    delta += ((py / 3) % 2 == 0) ? 7 : -7;
                } else {
                    delta += ((px + py) % 4 < 2) ? -12 : 4;
                }
                color = shifted(base, delta);
            }

            atlas.pixels[static_cast<std::size_t>((ty0 + py) * atlas.width + tx0 + px)] = color;
        }
    }
}

// Paints one 16x16 crack overlay tile (mining progress stage 0..9): opaque
// dark fracture lines growing from the tile center, deterministic per stage.
// Stage s paints (s + 1) random-walk branches whose length grows with s, so
// the damage reads as spreading cracks rather than noise.
void paint_crack_tile(AtlasImage &atlas, int tx0, int ty0, int stage) {
    const std::uint32_t crack_color = rgba(24, 20, 18, 210);
    const std::uint32_t seed = 0xC4AC7AEEU + static_cast<std::uint32_t>(stage) * 0x9E3779B9U;

    for (int branch = 0; branch <= stage + 1; ++branch) {
        // Deterministic walk start + direction from the branch index.
        const std::uint32_t h = hash2(branch, stage, seed);
        int x = 8 + static_cast<int>(h % 3) - 1;
        int y = 8 + static_cast<int>((h >> 4) % 3) - 1;
        int dx = static_cast<int>((h >> 8) % 3) - 1;
        int dy = static_cast<int>((h >> 12) % 3) - 1;
        if (dx == 0 && dy == 0) {
            dx = 1;
        }
        const int length = 3 + stage + static_cast<int>((h >> 16) % 3);
        for (int step = 0; step < length; ++step) {
            if (x >= 0 && x < kTileSize && y >= 0 && y < kTileSize) {
                atlas.pixels[static_cast<std::size_t>((ty0 + y) * atlas.width + tx0 + x)] = crack_color;
            }
            // Occasionally fork the walk for a jagged look.
            const std::uint32_t turn = hash2(x * 31 + step, y * 17 + branch, seed);
            if (turn % 4 == 0) {
                dx = static_cast<int>(turn % 3) - 1;
            } else if (turn % 4 == 1) {
                dy = static_cast<int>((turn >> 8) % 3) - 1;
            }
            if (dx == 0 && dy == 0) {
                dx = turn % 2 == 0 ? 1 : -1;
            }
            x += dx;
            y += dy;
        }
    }
}

} // namespace

AtlasImage generate_atlas(const voxel::BlockRegistry &registry) {
    const std::size_t tile_count = registry.size() * 3 + 10; // blocks + 10 crack stages
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(tile_count))));
    const int tiles_per_row = std::max(1, side);

    AtlasImage atlas;
    atlas.tiles_per_row = tiles_per_row;
    atlas.width = tiles_per_row * kTileSize;
    atlas.height = tiles_per_row * kTileSize;
    atlas.pixels.assign(static_cast<std::size_t>(atlas.width) * static_cast<std::size_t>(atlas.height),
                        rgba(30, 30, 34));

    for (std::uint16_t id = 0; id < registry.size(); ++id) {
        for (int slot = 0; slot < 3; ++slot) {
            const std::uint16_t tile = render::tile_index(id, slot);
            const int tx = (tile % static_cast<std::uint16_t>(tiles_per_row)) * kTileSize;
            const int ty = (tile / static_cast<std::uint16_t>(tiles_per_row)) * kTileSize;
            paint_tile(atlas, tx, ty, id, slot, registry.string_of(id));
        }
    }
    for (int stage = 0; stage < 10; ++stage) {
        const std::uint16_t tile = crack_tile_base(registry.size()) + static_cast<std::uint16_t>(stage);
        const int tx = (tile % static_cast<std::uint16_t>(tiles_per_row)) * kTileSize;
        const int ty = (tile / static_cast<std::uint16_t>(tiles_per_row)) * kTileSize;
        paint_crack_tile(atlas, tx, ty, stage);
    }
    return atlas;
}

} // namespace opencraft::client
