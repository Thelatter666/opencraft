#pragma once

// In-game HUD: hotbar (slot backdrops, selection frame, item icons, stack
// counts), the selected item name and the health hearts. Moved out of main.cpp
// by T-M1 (pure code motion); T-I2 replaced the hardcoded 9-block palette and
// the bucket cell with the real inventory's hotbar section, and added the
// count readout.

#include <cstdint>
#include <optional>
#include <span>

#include "crafting_ui.hpp"
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
    // T-D59 (appended; the fields above keep their order): how charged the held
    // weapon is, 0.2 … 1.0 - interaction.hpp's attack_charge(). Drawn as the
    // attack bar just above the hotbar, with the ⚖ 84.8% gate marked on it. It
    // is 1.0 before the first swing, which is what the authority charges for that
    // swing too (ruling C-2).
    double attack_charge = 1.0;
    // T-D45: the player is dead, so the HUD is not drawn at all. The death screen
    // is the whole UI in that state, and a hotbar under a "you died" panel reads
    // as a bug.
    bool dead = false;
};

// Depth test is disabled on entry and restored on exit (as before).
void draw_hud(const HudResources &res, const HudState &state);

// ── T-D60: the crafting screen ─────────────────────────────────────────────
// Everything the panel draws, assembled by the caller. `layout` decides every
// position AND is what the click was hit-tested against (one object, so what the
// player clicks is what they see); `result` is worked out once by the caller with
// craft_result_of() so the drawing never re-runs the matcher.
struct CraftingDrawState {
    CraftingLayout layout{};
    const game::Inventory *inventory = nullptr;
    const game::ItemRegistry *items = nullptr;
    VesselIds vessels;
    const CraftingState *crafting = nullptr;
    // The cell the pointer is over (draws a highlight frame); None for none.
    CraftSlotRef hover{};
    // What the grid makes right now, if anything.
    std::optional<game::RecipeResult> result;
    // Where the carried stack is drawn, in framebuffer pixels.
    float cursor_x = 0.0f;
    float cursor_y = 0.0f;
};

// Draws the panel over the live scene: a dim, the window, every cell's backdrop,
// the items in them, the counts, the hover frame and the stack on the pointer.
// Blend is enabled on entry and depth test/cull disabled, exactly like the death
// screen (whose contract is the same one); the caller restores GL state.
void draw_crafting_screen(const HudResources &res, const CraftingDrawState &state);

} // namespace opencraft::client
