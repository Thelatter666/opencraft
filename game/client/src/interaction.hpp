#pragma once

// Client-side interaction/carry state: the player's inventory, the hotbar
// selection, the input edges that drive placement, the targeting + crack
// overlay and the swing / sprint-jump QA counters. Moved out of main.cpp by
// T-M1 (pure code motion) so a logic tick and the renderer share the same
// named state instead of main()'s locals.
//
// T-I2 replaced the T-F1/T008 placeholders with the real model: the 9-item
// hardcoded block array and the `bucket_has_water` bool are gone, and the held
// item is whatever the selected inventory cell holds. Everything the renderer
// and the tick need about it is cached in selected_stack / selected_block /
// selected_use, refreshed by refresh_selection().

#include <cstdint>

#include <glm/glm.hpp>

#include "inventory_wiring.hpp"
#include "opencraft/game/inventory.hpp"
#include "opencraft/sim/entity_store.hpp"

namespace opencraft::client {

struct InteractionState {
    // ── inventory (T-I2) ────────────────────────────────────────────────────
    // The player's 41 cells. The hotbar the player sees is cells 0..8 of it,
    // so "the selected hotbar cell" is a plain index into this same numbering
    // (docs/01 §5 layout; game/inventory.hpp).
    game::Inventory inventory;
    // The T-F1 container pair as item ids (empty_vessel / water_vessel).
    VesselIds vessels;

    explicit InteractionState(const game::ItemRegistry &items) : inventory(items), vessels(resolve_vessel_ids(items)) {}

    // ── hotbar / held item ──────────────────────────────────────────────────
    int selected_slot = 0;
    // Cache of the selected cell, so the renderer and the tick's targeting do
    // not have to re-derive it. Only refresh_selection() writes these.
    game::ItemStack selected_stack{};
    // Block form of selected_stack; kNoBlock when the held item is not
    // placeable (the sentinel is never a valid block id -- see item_registry).
    std::uint16_t selected_block = game::kNoBlock;
    ItemUse selected_use = ItemUse::None;

    // ── input edges / retry cooldown ────────────────────────────────────────
    bool prev_w = false;
    bool prev_right = false;
    int place_cooldown = 0;
    // Attack cadence (T-M2). The base game spaces melee swings by the tool's
    // attack speed (docs/01 §4: a sword's is 1.6/s = 12.5 ticks) and scales the
    // damage by how far through the cooldown the swing is. This card only needs
    // the SPACING: the damage ramp belongs to the player-combat card, so a swing
    // here is always full damage (game::kPunchDamage) and the timer exists so a
    // held button does not become a machine gun.
    int attack_cooldown = 0;

    // ── targeting + mining overlay ──────────────────────────────────────────
    glm::ivec3 target_pos{0, 0, 0};
    bool has_target = false;
    glm::ivec3 crack_pos{0, 0, 0};
    int crack_stage = -1; // -1 = no overlay

    // ── entity target under the crosshair (T-M2) ────────────────────────────
    // The mob the view ray enters within game::kAttackReach, or kNoEntity.
    // Cached so the renderer can highlight it and the tick can decide whether a
    // click is an attack/interaction or a block action.
    server::EntityId picked_mob = server::EntityStore::kNoEntity;
    double picked_mob_distance = 0.0;

    // ── hand swing + T-D1 sprint-jump arc QA counters ───────────────────────
    bool swinging = false;
    double swing_start = -10.0; // glfwGetTime() of the last swing start
    // T-D1 QA evidence (acceptance 6c): measure each sprint-jump arc so the
    // on-machine distance can be compared against the headless ⚖ assertions
    // (arc average 7.127 ±1%, gap clearance ≈4 blocks). An arc opens on the
    // tick a grounded sprint jump leaves the ground and closes on landing.
    bool jump_arc_open = false;
    glm::dvec3 jump_arc_start{0.0, 0.0, 0.0};
    int jump_arc_ticks = 0;

    // Selects a hotbar cell (keys 1..9). Out-of-range values are ignored, so
    // the caller never has to clamp.
    void select_slot(int slot) {
        if (slot < 0 || slot >= game::kHotbarSlots) {
            return;
        }
        selected_slot = slot;
        refresh_selection();
    }

    // Drops everything that only means something for a LIVE player (T-D45 §2.5):
    // the crack overlay, the target highlight, the entity pick under the crosshair
    // and the two retry cooldowns. A corpse must keep none of it, and neither must
    // a player who has just respawned somewhere else - a stale target would draw
    // its wireframe around a block hundreds of blocks away. The mining tracker is
    // not in here (it is a separate object in the tick's context); its callers
    // reset it alongside.
    void clear_live_state() {
        crack_stage = -1;
        has_target = false;
        picked_mob = server::EntityStore::kNoEntity;
        picked_mob_distance = 0.0;
        swinging = false;
        place_cooldown = 0;
        attack_cooldown = 0;
    }

    // Re-reads the cache from the inventory. Call after anything that can
    // change the selected cell: a key, a placement, a vessel swap, load.
    void refresh_selection() {
        if (selected_slot < 0 || selected_slot >= game::kHotbarSlots) {
            selected_slot = 0; // never point outside the bar
        }
        selected_stack = inventory.slot(selected_slot);
        selected_block = placed_block_of(inventory.registry(), selected_stack);
        selected_use = item_use_of(inventory.registry(), selected_stack, vessels);
    }
};

} // namespace opencraft::client
