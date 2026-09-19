#pragma once

// T-I2: the client-side glue between the headless item/inventory model
// (game/common, T-I1) and the interaction / HUD code that reads it.
//
// Header-only and free of GL and GLFW on purpose: tests/ already puts this
// directory on its include path (fov.hpp, camera_spring.hpp do the same), so
// the selection rules, the vessel swap and the starting kit are unit-testable
// without opening a window.

#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

#include "opencraft/game/inventory.hpp"
#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/item_stack.hpp"

namespace opencraft::client {

// ── Held-item behaviour ───────────────────────────────────────────────────

// What the use button does with the held stack. The vessels are the T-F1
// bucket behaviour, now expressed as two item ids instead of a bool, so
// "empty in hand" and "full in hand" are two ordinary inventory states.
enum class ItemUse : std::uint8_t {
    None,       // no item, or an item whose use is not implemented yet
    PlaceBlock, // the item has a block form
    FillVessel, // empty vessel: scoop a water source out of the world
    PourVessel, // water vessel: pour a water source into the world
};

// The two item ids forming the T-F1 container pair, resolved from string ids
// once at startup (id_of throws if a card ever renames them, which is the
// intended failure mode for content drift).
struct VesselIds {
    std::uint16_t empty = game::ItemRegistry::kEmptyId;
    std::uint16_t full = game::ItemRegistry::kEmptyId;
};

[[nodiscard]] inline VesselIds resolve_vessel_ids(const game::ItemRegistry &items) {
    return {items.id_of("empty_vessel"), items.id_of("water_vessel")};
}

// The block the held stack places, or kNoBlock when the stack is empty or the
// item has no block form. This is the single place the sentinel is read.
[[nodiscard]] inline std::uint16_t placed_block_of(const game::ItemRegistry &items, const game::ItemStack &stack) {
    if (stack.empty()) {
        return game::kNoBlock;
    }
    return items.def_of(stack.item).block;
}

// What the use button does with the held stack.
[[nodiscard]] inline ItemUse item_use_of(const game::ItemRegistry &items, const game::ItemStack &stack,
                                         const VesselIds &vessels) {
    if (stack.empty()) {
        return ItemUse::None;
    }
    if (stack.item == vessels.empty) {
        return ItemUse::FillVessel;
    }
    if (stack.item == vessels.full) {
        return ItemUse::PourVessel;
    }
    if (items.def_of(stack.item).block != game::kNoBlock) {
        return ItemUse::PlaceBlock;
    }
    return ItemUse::None;
}

// True for the two vessel states, i.e. the actions that are edge-triggered
// rather than repeating on the 4-tick placement rhythm (see run_tick).
[[nodiscard]] constexpr bool is_vessel_use(ItemUse use) {
    return use == ItemUse::FillVessel || use == ItemUse::PourVessel;
}

// ── Vessel conversion ─────────────────────────────────────────────────────

// Dry run of transform_vessel() on a copy: true when one `from` in `slot` can
// become one `to` without losing an item. Taking the unit out of `slot` frees
// that cell, so the conversion only fails when the cell holds a bigger stack
// and every other storage cell is full of something else.
//
// The caller checks this BEFORE touching the world: scooping a source and then
// finding nowhere to put the vessel would delete it.
[[nodiscard]] inline bool can_transform_vessel(const game::Inventory &inventory, int slot, std::uint16_t from,
                                               std::uint16_t to) {
    if (!game::Inventory::is_valid_slot(slot)) {
        return false;
    }
    const game::ItemStack held = inventory.slot(slot);
    if (held.item != from || held.count < 1) {
        return false;
    }
    game::Inventory trial = inventory;
    const int taken = trial.remove_from_slot(slot, 1);
    game::ItemStack produced = game::ItemStack::of(to, 1);
    const game::AddResult added = trial.add_item(produced);
    return taken == 1 && added.remaining == 0;
}

// Turns one `from` in `slot` into one `to`, keeping the container in hand.
//
// A cell holding a single container becomes the other state IN PLACE: that is
// what "the empty vessel becomes a water vessel" means for the player, and it
// keeps the container where they were holding it. Only a bigger stack has to
// spill, because the produced state is a 1-stack that cannot join the units
// left behind -- then it goes into the storage sections (hotbar first, then
// main, the same order every pickup uses).
//
// Returns false, changing nothing, when can_transform_vessel() would say no.
inline bool transform_vessel(game::Inventory &inventory, int slot, std::uint16_t from, std::uint16_t to) {
    if (!can_transform_vessel(inventory, slot, from, to)) {
        return false;
    }
    if (inventory.slot(slot).count == 1) {
        return inventory.set_slot(slot, game::ItemStack::of(to, 1));
    }
    const int taken = inventory.remove_from_slot(slot, 1);
    game::ItemStack produced = game::ItemStack::of(to, 1);
    const game::AddResult added = inventory.add_item(produced);
    return taken == 1 && added.remaining == 0;
}

// ── Placement bookkeeping ─────────────────────────────────────────────────

// What spending one unit of a stack on a placement did.
struct PlacementResult {
    std::uint16_t block = game::kNoBlock; // the block to write into the world
    int consumed = 0;                     // units taken out of the cell (0 or 1)
    int left = 0;                         // units left in the cell afterwards
};

// Spends one unit of `slot`'s stack on a block placement and reports the block
// that was placed, read WHILE THE CELL STILL HOLDS IT.
//
// That ordering is the point of this function: once the last unit leaves the
// cell, the cell is empty and the caller's selection cache says kNoBlock, and
// handing that sentinel to the block registry throws. Found on-machine
// 2026-09-16 as a crash (`unknown block numeric id`, from a placement log line
// that re-read the cache after emptying the cell), hence this helper.
//
// Returns {kNoBlock, 0, left} when the slot holds nothing placeable, so an
// empty hand, a tool or a vessel never spends anything.
[[nodiscard]] inline PlacementResult place_one_block(game::Inventory &inventory, int slot) {
    PlacementResult result;
    if (!game::Inventory::is_valid_slot(slot)) {
        return result;
    }
    const game::ItemStack held = inventory.slot(slot);
    result.left = held.count;
    result.block = placed_block_of(inventory.registry(), held);
    if (result.block == game::kNoBlock) {
        return result;
    }
    result.consumed = inventory.remove_from_slot(slot, 1);
    result.left = inventory.slot(slot).count;
    return result;
}

// ── Presentation ──────────────────────────────────────────────────────────

// A block cube standing in for a stack. The client can draw block cubes in
// the first-person hand and textured tiles in the hotbar; anything else has no
// visual yet.
struct StandInVisual {
    std::uint16_t block = game::kNoBlock;
    std::uint8_t shade = 235; // mesher shade scale (0..255)
};

// The cube that stands in for `stack` in the hand and in its hotbar cell.
// Block items show their own block; a vessel shows the water it carries,
// dimmed while empty -- field-for-field the T-F1 bucket rendering, now driven
// by the held item instead of a boolean. Other items (food, tools, armour)
// have no cube and fall back to the flat tint below.
[[nodiscard]] inline StandInVisual stand_in_visual_of(const game::ItemRegistry &items, const game::ItemStack &stack,
                                                      std::uint16_t water_block, const VesselIds &vessels) {
    if (stack.empty()) {
        return {};
    }
    if (stack.item == vessels.full) {
        return {water_block, 235};
    }
    if (stack.item == vessels.empty) {
        return {water_block, 70};
    }
    return {items.def_of(stack.item).block, 235};
}

// Flat placeholder tint for the items that have no generated texture (food,
// tools, armour). Original colours chosen by item id; unknown ids get a
// neutral grey so a new content card still renders something. A real item
// icon set is a content card's job, not this wiring card's.
[[nodiscard]] inline glm::vec3 item_tint(std::string_view id) {
    if (id == "sunroot") {
        return {0.46f, 0.66f, 0.28f};
    }
    if (id == "cave_cap") {
        return {0.58f, 0.42f, 0.34f};
    }
    if (id == "grain_loaf") {
        return {0.74f, 0.58f, 0.30f};
    }
    if (id == "char_lump") {
        return {0.24f, 0.24f, 0.26f};
    }
    if (id == "ferrous_bloom") {
        return {0.62f, 0.50f, 0.44f};
    }
    if (id == "rime_pearl") {
        return {0.78f, 0.86f, 0.92f};
    }
    if (id == "timber_chisel" || id == "timber_hewer" || id == "timber_spade" || id == "timber_edge") {
        return {0.60f, 0.46f, 0.30f}; // the timber tool tier
    }
    // T-D60: the rock tier is grey where the timber tier is brown, so the two
    // tiers read apart in a grid cell at 24 px (the screenshot chain of the
    // card's §5.4 is the reason this matters at all). The stick is a pale wood
    // tone, distinct from both and from the planks it is crafted from.
    if (id == "rock_chisel" || id == "rock_hewer" || id == "rock_spade" || id == "rock_edge") {
        return {0.52f, 0.53f, 0.56f}; // the rock tool tier
    }
    if (id == "timber_stick") {
        return {0.72f, 0.62f, 0.42f};
    }
    if (id == "timber_headguard" || id == "timber_cuirass" || id == "timber_greaves" || id == "timber_treads") {
        return {0.72f, 0.58f, 0.36f}; // the timber armour tier
    }
    return {0.55f, 0.55f, 0.58f};
}

// ── Mob stand-in skins (T-M2) ─────────────────────────────────────────────
// A mob is drawn as two boxes (a body and a head) textured with tiles from the
// block atlas, because there is no mob model, no mob texture set and no
// flat-colour box shader in the engine yet - inventing any of those is an art /
// render card's work, not this one's.
//
// The names and the colour CHOICES are original (docs/04 red line 2/5): a
// Mossback is a brown egg with a green crown (it grazes), a Hollow Wretch is
// grey rock, and a Blastbud is a green pod with a pale cap. The look is a
// placeholder that makes behaviour observable; the report says so explicitly.
struct MobSkin {
    const char *body_block;
    const char *head_block;
    // Head size as a fraction of the body's width/height, and how far forward it
    // sits. Chosen for readability, not measured from anything.
    double head_scale = 0.6;
    double head_forward = 0.35;
};

[[nodiscard]] inline MobSkin mob_skin(std::string_view mob_id) {
    // ⚠ BLOCK ids, not item ids: these index the block atlas (block*3 + face).
    // Getting that wrong is a hard crash, not a wrong colour - found on-machine by
    // exactly that crash ("unknown block id: loam_clod").
    if (mob_id == "mossback") {
        return {"dirt", "grass_block", 0.7, 0.4}; // brown body, green crown: it grazes
    }
    if (mob_id == "hollow_wretch") {
        return {"cobblestone", "gravel", 0.55, 0.25}; // grey rock
    }
    if (mob_id == "blastbud") {
        return {"leaves", "sandstone", 0.75, 0.45}; // a green pod with a pale cap
    }
    return {"stone", "stone", 0.6, 0.3}; // an unknown mob still renders
}

// ── Starting kit ──────────────────────────────────────────────────────────

// One cell of the launch kit, by item string id and count.
struct StartingStack {
    int slot;
    const char *item;
    int count;
};

// The hotbar the player spawns with. It samples all three documented stack
// tiers (docs/01 §5: 64 / 16 / 1) and both vessel states, so the count
// readout, the "64 vs 16 vs 1" ladder and the T-F1 scoop/pour pair are all
// reachable without opening anything.
inline constexpr StartingStack kStartingHotbar[] = {
    {0, "greyrock", 64},     // 64-tier, placeable
    {1, "sod_loam", 32},     // partial stack of a 64-tier item
    {2, "sawn_planks", 8},   //
    {3, "duskglass", 4},     // dark, easy to tell apart from the greys
    {4, "empty_vessel", 16}, // 16-tier, at its limit
    {5, "water_vessel", 1},  // 1-tier; pouring is available immediately
    {6, "grain_loaf", 5},    // no block form: the flat-tint fallback
    {7, "timber_chisel", 1}, // 1-tier, no block form
    {8, "rime_block", 3},    // white, the last 64-tier sample
};

// The main section ships content too (ores, food, tools, one armour piece per
// body part) so the storage cards have something to move around.
inline constexpr StartingStack kStartingMain[] = {
    {9, "char_ore", 12},       {10, "ferrous_ore", 6},    {11, "lucent_ore", 2},
    {12, "sunroot", 9},        {13, "timber_hewer", 1},   {14, "timber_headguard", 1},
    {15, "timber_cuirass", 1}, {16, "timber_greaves", 1}, {17, "timber_treads", 1},
    // T-D60 裁决 C-7: the sword and the shovel LEFT the kit, two cards after
    // T-D59's S-4 put them in "until the crafting card lands". The card has
    // landed: both are craftable from planks and a stick now, so granting them
    // would hand the player the tier the progression is supposed to produce
    // (and the empty cells are where the first crafted tools land). The pick
    // and the axe stay - they are the tools the chain STARTS from.
};

// Fills the launch kit into a fresh inventory. Slots are written exactly, so a
// count above the item's stack limit is refused by the inventory rather than
// clamped (the wiring test asserts every entry lands).
//
// No persistence: the kit is re-created on every launch (inventory saving is
// M2c, per the card's scope).
inline void fill_starting_inventory(game::Inventory &inventory) {
    const game::ItemRegistry &items = inventory.registry();
    for (const StartingStack &entry : kStartingHotbar) {
        inventory.set_slot(entry.slot, game::ItemStack::of(items.id_of(entry.item), entry.count));
    }
    for (const StartingStack &entry : kStartingMain) {
        inventory.set_slot(entry.slot, game::ItemStack::of(items.id_of(entry.item), entry.count));
    }
}

} // namespace opencraft::client
