// T-I2: the client wiring between the headless inventory (game/common) and
// the interaction/HUD code. Everything here is header-only and GL-free, so the
// selection rules, the vessel swap and the launch kit are testable without a
// window -- the same trick test_sprint_feel.cpp uses for the client's fov.hpp.
//
// Naming note: TEST_CASE names deliberately avoid '[' (card acceptance 9).

#include <cstddef>
#include <cstdint>
#include <iterator>

#include <doctest/doctest.h>
#include <glm/glm.hpp>

#include "interaction.hpp"
#include "inventory_wiring.hpp"
#include "opencraft/game/inventory.hpp"
#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/item_stack.hpp"
#include "opencraft/voxel/block_registry.hpp"

namespace {

using opencraft::client::can_transform_vessel;
using opencraft::client::fill_starting_inventory;
using opencraft::client::InteractionState;
using opencraft::client::is_vessel_use;
using opencraft::client::item_tint;
using opencraft::client::item_use_of;
using opencraft::client::ItemUse;
using opencraft::client::kStartingHotbar;
using opencraft::client::kStartingMain;
using opencraft::client::place_one_block;
using opencraft::client::placed_block_of;
using opencraft::client::resolve_vessel_ids;
using opencraft::client::stand_in_visual_of;
using opencraft::client::StartingStack;
using opencraft::client::transform_vessel;
using opencraft::client::VesselIds;
using opencraft::game::Inventory;
using opencraft::game::ItemRegistry;
using opencraft::game::ItemStack;
using opencraft::game::kArmorFirstSlot;
using opencraft::game::kHotbarSlots;
using opencraft::game::kInventorySlots;
using opencraft::game::kNoBlock;
using opencraft::game::kStackLimitLarge;
using opencraft::voxel::BlockRegistry;

} // namespace

TEST_CASE("wiring placed block comes from the item link and never from the sentinel") {
    const auto items = ItemRegistry::create_default();
    const auto blocks = BlockRegistry::create_default();

    CHECK(placed_block_of(items, ItemStack::of(items.id_of("greyrock"), 64)) == blocks.id_of("stone"));
    CHECK(placed_block_of(items, ItemStack::of(items.id_of("loam_clod"), 1)) == blocks.id_of("dirt"));
    CHECK(placed_block_of(items, ItemStack::of(items.id_of("duskglass"), 1)) == blocks.id_of("obsidian"));

    // Items with no block form, and the empty stack, answer kNoBlock -- which
    // is not a legal block id, so "places nothing" can never be confused with
    // "places air" (block id 0).
    CHECK(placed_block_of(items, ItemStack::of(items.id_of("empty_vessel"), 1)) == kNoBlock);
    CHECK(placed_block_of(items, ItemStack::of(items.id_of("timber_chisel"), 1)) == kNoBlock);
    CHECK(placed_block_of(items, ItemStack{}) == kNoBlock);
    CHECK(kNoBlock != BlockRegistry::kAirId);
    CHECK_FALSE(blocks.has_numeric(kNoBlock));
}

TEST_CASE("wiring use action depends on the held stack, vessels are not a boolean") {
    const auto items = ItemRegistry::create_default();
    const VesselIds vessels = resolve_vessel_ids(items);

    CHECK(vessels.empty != vessels.full);
    CHECK(vessels.empty != ItemRegistry::kEmptyId);
    CHECK(vessels.full != ItemRegistry::kEmptyId);

    CHECK(item_use_of(items, ItemStack::of(items.id_of("greyrock"), 5), vessels) == ItemUse::PlaceBlock);
    CHECK(item_use_of(items, ItemStack::of(vessels.empty, 1), vessels) == ItemUse::FillVessel);
    CHECK(item_use_of(items, ItemStack::of(vessels.full, 1), vessels) == ItemUse::PourVessel);
    CHECK(item_use_of(items, ItemStack::of(items.id_of("grain_loaf"), 3), vessels) == ItemUse::None);
    CHECK(item_use_of(items, ItemStack::of(items.id_of("timber_headguard"), 1), vessels) == ItemUse::None);
    CHECK(item_use_of(items, ItemStack{}, vessels) == ItemUse::None);

    // Only the vessels are edge-triggered; the block path keeps its 4-tick
    // retry rhythm (T-F1 follow-up, see run_tick).
    CHECK(is_vessel_use(ItemUse::FillVessel));
    CHECK(is_vessel_use(ItemUse::PourVessel));
    CHECK_FALSE(is_vessel_use(ItemUse::PlaceBlock));
    CHECK_FALSE(is_vessel_use(ItemUse::None));
}

TEST_CASE("wiring selection follows the inventory cell and ignores out-of-range keys") {
    const auto items = ItemRegistry::create_default();
    InteractionState state(items);
    fill_starting_inventory(state.inventory);
    state.refresh_selection();

    CHECK(state.selected_slot == 0);
    CHECK(state.selected_stack.item == items.id_of("greyrock"));
    CHECK(state.selected_block == items.def_of(items.id_of("greyrock")).block);
    CHECK(state.selected_use == ItemUse::PlaceBlock);

    for (int slot = 0; slot < kHotbarSlots; ++slot) {
        INFO("hotbar key " << slot + 1);
        state.select_slot(slot);
        CHECK(state.selected_slot == slot);
        CHECK(state.selected_stack.item == state.inventory.slot(slot).item);
    }

    // The launch kit's cells 5 and 6 are the two container states, so the T-F1
    // scoop/pour pair is one key press away in both directions.
    state.select_slot(4);
    CHECK(state.selected_use == ItemUse::FillVessel);
    CHECK(state.selected_block == kNoBlock);
    state.select_slot(5);
    CHECK(state.selected_use == ItemUse::PourVessel);
    CHECK(state.selected_block == kNoBlock);

    // An out-of-range selection is ignored, never wrapped onto another cell.
    state.select_slot(-1);
    CHECK(state.selected_slot == 5);
    state.select_slot(kHotbarSlots);
    CHECK(state.selected_slot == 5);
}

TEST_CASE("wiring placement consumes one unit and empties the cell at zero") {
    const auto items = ItemRegistry::create_default();
    InteractionState state(items);
    state.inventory.set_slot(0, ItemStack::of(items.id_of("rime_block"), 3));
    state.select_slot(0);

    // The tick's placement sequence, step for step: put the block, take one
    // unit out of the selected cell, re-derive the cache.
    for (int remaining = 2; remaining >= 0; --remaining) {
        CHECK(state.selected_block == items.def_of(items.id_of("rime_block")).block);
        CHECK(state.inventory.remove_from_slot(state.selected_slot, 1) == 1);
        state.refresh_selection();
        CHECK(state.selected_stack.count == remaining);
    }

    // Used up: the cell is empty and the hand has nothing left to place.
    CHECK(state.inventory.slot(0).empty());
    CHECK(state.selected_stack.empty());
    CHECK(state.selected_block == kNoBlock);
    CHECK(state.selected_use == ItemUse::None);
}

TEST_CASE("wiring placing the last unit still reports the block it placed") {
    const auto items = ItemRegistry::create_default();
    Inventory inventory(items);
    const std::uint16_t rime = items.id_of("rime_block");
    const std::uint16_t rime_block = items.def_of(rime).block;
    inventory.set_slot(2, ItemStack::of(rime, 2));

    // Spending a unit reports the block and the count that is left.
    const auto first = place_one_block(inventory, 2);
    CHECK(first.block == rime_block);
    CHECK(first.consumed == 1);
    CHECK(first.left == 1);

    // Spending the LAST unit still reports the block. This is the on-machine
    // crash of 2026-09-16: reading the block after the cell emptied handed the
    // kNoBlock sentinel to the block registry, which throws.
    const auto last = place_one_block(inventory, 2);
    CHECK(last.block == rime_block);
    CHECK(last.consumed == 1);
    CHECK(last.left == 0);
    CHECK(inventory.slot(2).empty());

    // Nothing placeable left: the sentinel comes back and nothing is spent.
    const auto exhausted = place_one_block(inventory, 2);
    CHECK(exhausted.block == kNoBlock);
    CHECK(exhausted.consumed == 0);
    CHECK(exhausted.left == 0);

    // A non-placeable item never spends a unit either.
    inventory.set_slot(3, ItemStack::of(items.id_of("grain_loaf"), 5));
    const auto loaf = place_one_block(inventory, 3);
    CHECK(loaf.block == kNoBlock);
    CHECK(loaf.consumed == 0);
    CHECK(inventory.slot(3).count == 5);

    // An invalid slot is refused, not undefined.
    const auto bad = place_one_block(inventory, kInventorySlots);
    CHECK(bad.block == kNoBlock);
    CHECK(bad.consumed == 0);
}

TEST_CASE("wiring vessel swap moves exactly one unit between the two states") {
    const auto items = ItemRegistry::create_default();
    const VesselIds vessels = resolve_vessel_ids(items);
    Inventory inventory(items);

    // Scooping from a 16-stack: one unit leaves the cell and the filled vessel
    // lands in the next free cell, because it is a 1-stack of its own and the
    // fifteen units left behind stay put.
    inventory.set_slot(0, ItemStack::of(vessels.empty, 16));
    CHECK(can_transform_vessel(inventory, 0, vessels.empty, vessels.full));
    CHECK(transform_vessel(inventory, 0, vessels.empty, vessels.full));
    CHECK(inventory.slot(0).item == vessels.empty);
    CHECK(inventory.slot(0).count == 15);
    CHECK(inventory.slot(1).item == vessels.full);
    CHECK(inventory.slot(1).count == 1);

    // Pouring that single filled vessel keeps it in the cell it was in: the
    // player is still holding a container afterwards, which is what "the
    // water vessel becomes an empty vessel" has to mean in hand. (It does not
    // merge into the 15-stack next door -- merging would move the container
    // out of the player's hand.)
    CHECK(transform_vessel(inventory, 1, vessels.full, vessels.empty));
    CHECK(inventory.slot(1).item == vessels.empty);
    CHECK(inventory.slot(1).count == 1);
    CHECK(inventory.slot(0).count == 15);
    CHECK(inventory.count_of(vessels.full) == 0);
    CHECK(inventory.count_of(vessels.empty) == 16); // nothing lost either way

    // A swap of the wrong item, or from an empty cell, changes nothing.
    CHECK_FALSE(transform_vessel(inventory, 0, vessels.full, vessels.empty));
    CHECK(inventory.slot(0).count == 15);
    CHECK_FALSE(transform_vessel(inventory, 30, vessels.empty, vessels.full));
    CHECK(inventory.slot(0).count == 15);
}

TEST_CASE("wiring vessel swap is refused when the container would have nowhere to go") {
    const auto items = ItemRegistry::create_default();
    const VesselIds vessels = resolve_vessel_ids(items);
    Inventory inventory(items);

    // Cell 0 holds 16 vessels (15 remain after the scoop) and every other
    // storage cell is a full stack of something else, so the filled vessel has
    // nowhere to land. The swap must be refused BEFORE the world is touched:
    // scooping first and failing after would delete a vessel.
    inventory.set_slot(0, ItemStack::of(vessels.empty, 16));
    for (int slot = 1; slot < kArmorFirstSlot; ++slot) {
        REQUIRE(inventory.set_slot(slot, ItemStack::of(items.id_of("greyrock"), kStackLimitLarge)));
    }
    CHECK_FALSE(can_transform_vessel(inventory, 0, vessels.empty, vessels.full));
    CHECK_FALSE(transform_vessel(inventory, 0, vessels.empty, vessels.full));
    CHECK(inventory.slot(0).count == 16);
    CHECK(inventory.count_of(vessels.full) == 0);

    // Free one cell and the same scoop becomes possible.
    REQUIRE(inventory.set_slot(20, ItemStack{}));
    CHECK(can_transform_vessel(inventory, 0, vessels.empty, vessels.full));
    CHECK(transform_vessel(inventory, 0, vessels.empty, vessels.full));
    CHECK(inventory.slot(0).count == 15);
    CHECK(inventory.count_of(vessels.full) == 1);

    // A lone vessel always fits: emptying the cell frees the cell it goes into.
    Inventory single(items);
    single.set_slot(3, ItemStack::of(vessels.full, 1));
    for (int slot = 0; slot < kArmorFirstSlot; ++slot) {
        if (slot != 3) {
            REQUIRE(single.set_slot(slot, ItemStack::of(items.id_of("greyrock"), kStackLimitLarge)));
        }
    }
    CHECK(transform_vessel(single, 3, vessels.full, vessels.empty));
    CHECK(single.slot(3).item == vessels.empty);
    CHECK(single.slot(3).count == 1);
}

TEST_CASE("wiring stand-in visuals cover block items, both vessels and the fallback") {
    const auto items = ItemRegistry::create_default();
    const auto blocks = BlockRegistry::create_default();
    const VesselIds vessels = resolve_vessel_ids(items);
    const std::uint16_t water = blocks.id_of("water");

    // A block item shows its own block, at full shade.
    const auto rock = stand_in_visual_of(items, ItemStack::of(items.id_of("greyrock"), 3), water, vessels);
    CHECK(rock.block == blocks.id_of("stone"));
    CHECK(rock.shade == 235);

    // A vessel shows the water it carries; the empty one is dimmed, which is
    // the T-F1 bucket look (bright when full, dim when empty).
    const auto full = stand_in_visual_of(items, ItemStack::of(vessels.full, 1), water, vessels);
    const auto empty = stand_in_visual_of(items, ItemStack::of(vessels.empty, 1), water, vessels);
    CHECK(full.block == water);
    CHECK(empty.block == water);
    CHECK(empty.shade < full.shade);

    // Items with no cube, and the empty hand: the hotbar draws the flat tint
    // instead (or nothing at all for an empty cell).
    CHECK(stand_in_visual_of(items, ItemStack::of(items.id_of("grain_loaf"), 4), water, vessels).block == kNoBlock);
    CHECK(stand_in_visual_of(items, ItemStack::of(items.id_of("timber_treads"), 1), water, vessels).block == kNoBlock);
    CHECK(stand_in_visual_of(items, ItemStack{}, water, vessels).block == kNoBlock);
}

TEST_CASE("wiring item tints stay in range and tell the placeholder families apart") {
    const auto tint_of = [](const char *id) { return item_tint(id); };
    const auto valid = [](const glm::vec3 &tint) {
        return tint.r >= 0.0f && tint.r <= 1.0f && tint.g >= 0.0f && tint.g <= 1.0f && tint.b >= 0.0f && tint.b <= 1.0f;
    };

    CHECK(valid(tint_of("grain_loaf")));
    CHECK(valid(tint_of("timber_chisel")));
    CHECK(valid(tint_of("timber_headguard")));
    CHECK(tint_of("grain_loaf") != tint_of("timber_chisel"));

    // Unknown ids still render something (a new content card must not produce
    // an invisible hotbar cell), and the fallback is stable.
    CHECK(tint_of("not_an_item") == tint_of("also_not_an_item"));
    CHECK(valid(tint_of("not_an_item")));
}

TEST_CASE("wiring launch kit lands in the inventory and samples the three stack tiers") {
    const auto items = ItemRegistry::create_default();
    const VesselIds vessels = resolve_vessel_ids(items);
    Inventory inventory(items);
    fill_starting_inventory(inventory);

    CHECK(std::size(kStartingHotbar) == static_cast<std::size_t>(kHotbarSlots));

    for (const StartingStack &entry : kStartingHotbar) {
        INFO("starting hotbar cell " << entry.slot << " holding " << entry.item);
        const ItemStack cell = inventory.slot(entry.slot);
        CHECK(cell.item == items.id_of(entry.item));
        CHECK(cell.count == entry.count);
        CHECK(cell.count <= items.def_of(cell.item).max_stack);
    }
    for (const StartingStack &entry : kStartingMain) {
        INFO("starting main cell " << entry.slot << " holding " << entry.item);
        const ItemStack cell = inventory.slot(entry.slot);
        CHECK(cell.item == items.id_of(entry.item));
        CHECK(cell.count == entry.count);
        CHECK(cell.count <= items.def_of(cell.item).max_stack);
    }

    // All three documented tiers (docs/01 §5) are sampled on the visible bar,
    // so the count readout can be checked against the ladder at a glance, and
    // both container states are there -- T-F1's scoop and pour are one key
    // press away in either direction.
    bool large = false;
    bool medium = false;
    bool single = false;
    bool has_empty_vessel = false;
    bool has_full_vessel = false;
    int visible = 0;
    for (int slot = 0; slot < kHotbarSlots; ++slot) {
        const ItemStack cell = inventory.slot(slot);
        if (cell.empty()) {
            continue;
        }
        ++visible;
        large = large || items.def_of(cell.item).max_stack == 64;
        medium = medium || items.def_of(cell.item).max_stack == 16;
        single = single || items.def_of(cell.item).max_stack == 1;
        has_empty_vessel = has_empty_vessel || cell.item == vessels.empty;
        has_full_vessel = has_full_vessel || cell.item == vessels.full;
    }
    CHECK(large);
    CHECK(medium);
    CHECK(single);
    CHECK(has_empty_vessel);
    CHECK(has_full_vessel);
    CHECK(visible == static_cast<int>(std::size(kStartingHotbar)));

    // The kit only writes storage cells; armour and offhand stay empty.
    for (int slot = kArmorFirstSlot; slot < kInventorySlots; ++slot) {
        INFO("armour/offhand cell " << slot);
        CHECK(inventory.slot(slot).empty());
    }
}
