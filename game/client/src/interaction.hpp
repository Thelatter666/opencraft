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

// T-D59: "no swing yet" in the client's mirror of the charge clock. Any age at
// or past T reads as a full charge anyway (the ramp clamps), so the sentinel
// only has to be a value no real age can take - and negative is that.
inline constexpr int kNeverAttacked = -1;

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
    // T-D59: the left button's edge. An attack is a CLICK, not a hold (ruling
    // C-1) - see attack_age below for why the hold had to go.
    bool prev_left = false;
    int place_cooldown = 0;
    // Attack charge (T-D59, ruling C-2). This replaces T-M2's 12-tick
    // attack_cooldown countdown, whose only job was to stop a held button from
    // becoming a machine gun: the swing is edge-triggered now, so the spacing is
    // the player's own, and what is left to track is how long the weapon has
    // been charging.
    //
    // Ticks since the last swing the authority ACCEPTED, or kNeverAttacked. It
    // feeds the charge bar and nothing else - the damage is settled on the
    // authority's own copy of this number (WorldSim::last_attack_tick_), and
    // this one is only the mirror M3 will reconcile. If the two ever disagree,
    // the authority is right.
    int attack_age = kNeverAttacked;

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
    //
    // T-D59 put attack_age in the same group for the same reason: a respawned
    // player has not swung, and charging their first swing from the corpse's
    // clock would hand them a 20.8% hit they never earned.
    void clear_live_state() {
        crack_stage = -1;
        has_target = false;
        picked_mob = server::EntityStore::kNoEntity;
        picked_mob_distance = 0.0;
        swinging = false;
        place_cooldown = 0;
        attack_age = kNeverAttacked;
        prev_left = false;
    }

    // How charged the held weapon is, 0.2 … 1.0 - what the HUD bar draws. The
    // weapon's speed is read fresh from the selected cell every call, so the bar
    // follows a hotbar switch on the same frame (the authority does the same per
    // swing; neither caches).
    [[nodiscard]] double attack_charge() const {
        if (attack_age == kNeverAttacked) {
            return 1.0; // the first swing of a session is always full (C-2)
        }
        const game::ItemDef &held = inventory.registry().def_of(selected_stack.item);
        return game::attack_charge_multiplier(static_cast<double>(attack_age), game::attack_speed_of(held));
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
