#pragma once

// In-game HUD: hotbar (slot backdrops, selection frame, block icons), the
// selected item name and the health hearts. Moved verbatim out of main.cpp by
// T-M1 (pure code motion): every colour, offset, size, draw order and GL state
// change is unchanged; only the inputs became explicit parameters.

#include <cstdint>
#include <span>

#include "interaction.hpp"
#include "opencraft/render/rhi.hpp"

namespace opencraft::client {

class WorldSource;

// GL resources the HUD draws through. Built once at startup; all of them
// outlive the frame loop.
struct HudResources {
    const WorldSource &world;
    const render::Shader &terrain_shader; // slot icons reuse the terrain tile math
    const render::Shader &flat_shader;    // backdrops and the selection frame
    const render::Shader &text_shader;    // bitmap-font strings
    const render::Texture2D &font;
};

// Per-frame HUD inputs.
struct HudState {
    int fb_width = 0;
    int fb_height = 0;
    std::span<const std::uint16_t, 9> hotbar; // 9 block slots; the bucket is kBucketSlot
    int selected_slot = 0;
    bool bucket_selected = false;
    bool bucket_has_water = false;
    std::uint16_t selected_block = 0; // id shown/placed for the current slot
    double health = 0.0;
};

// Depth test is disabled on entry and restored on exit (as before).
void draw_hud(const HudResources &res, const HudState &state);

} // namespace opencraft::client
