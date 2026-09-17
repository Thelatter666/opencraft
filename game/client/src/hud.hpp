#pragma once

// In-game HUD: hotbar (slot backdrops, selection frame, item icons, stack
// counts), the selected item name and the health hearts. Moved out of main.cpp
// by T-M1 (pure code motion); T-I2 replaced the hardcoded 9-block palette and
// the bucket cell with the real inventory's hotbar section, and added the
// count readout.

#include <cstdint>
#include <span>

#include "interaction.hpp"
#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/item_stack.hpp"
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

// Per-frame HUD inputs. `hotbar` is the inventory's hotbar section, drawn as
// it is: empty cells draw nothing, cells above 1 unit draw their count.
struct HudState {
    int fb_width = 0;
    int fb_height = 0;
    std::span<const game::ItemStack, game::kHotbarSlots> hotbar;
    // Item names and the item -> block link for the icons.
    const game::ItemRegistry *items = nullptr;
    // The vessel pair, for the "shows the water it carries" icon rule.
    VesselIds vessels;
    int selected_slot = 0;
    double health = 0.0;
    // T-D45 (appended; the fields above keep their order): the player is dead,
    // so the HUD is not drawn at all. The death screen is the whole UI in that
    // state, and a hotbar under a "you died" panel reads as a bug.
    bool dead = false;
};

// Depth test is disabled on entry and restored on exit (as before).
void draw_hud(const HudResources &res, const HudState &state);

} // namespace opencraft::client
