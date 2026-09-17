#pragma once

// T-D45: the player's life cycle, on the client, where the hit points it guards
// already live. The authority deliberately does not own the player - a mob hit
// reaches the client as an ActorEvent and the client applies it
// (game/protocol.hpp) - so the death state cannot live there either; moving the
// player up is M3's work, with the rest of the session.
//
// The three rules this file holds, and why they are here rather than at the
// call sites (the card's §2.1 asks for exactly this):
//   * ONE damage entry. Three code paths used to move the hit points (the
//     physics step's fall, the mob/explosion events, the load-from-save
//     restore), and they disagreed: the fall left health negative, the events
//     floored at 0, the load restored whatever was in the file.
//   * The DEATH EDGE, not the dead state, is what triggers a death. The hits
//     arrive as a stream (one ActorEvent per tick a mob keeps hitting), so a
//     state test would drop the whole inventory again every tick spent dead.
//   * The respawn point is its own thing. The session's start position drifts
//     with the player (main.cpp stores the last quit position as `spawn_pos`),
//     so a respawn that used it would send a player who died far from home back
//     to wherever they last quit.

#include <algorithm>
#include <cstdint>

#include <glm/glm.hpp>

#include "gamerule.hpp"
#include "opencraft/game/inventory.hpp"
#include "opencraft/game/item_stack.hpp"
#include "opencraft/game/protocol.hpp"
#include "opencraft/physics/player_state.hpp"
#include "opencraft/storage/level_file.hpp"

namespace opencraft::client {

// ⚖ docs/01 §3: 生命 20 HP = 10 hearts. The upper bound of every write below,
// including a respawn's heal.
inline constexpr double kMaxHealth = 20.0;

// Where the player is in the death cycle, plus the place a respawn returns
// them to. Value data, no pointers: M3 ships the same fields to a server.
struct PlayerLife {
    bool dead = false;
    // Where the corpse was, i.e. the origin of the death drop (card §2.3).
    glm::dvec3 death_pos{0.0, 0.0, 0.0};
    // Where a respawn puts the player. NOT the session start position - see the
    // header comment - and it must survive save/load unchanged (card §2.4).
    glm::dvec3 respawn_pos{0.0, 0.0, 0.0};
};

// What one damage application did.
struct DamageResult {
    double health = kMaxHealth;
    // How much was actually subtracted: 0 when the hit was discarded (the player
    // was already dead) or healed nothing. One meaning for every path - the fall
    // path's measured delta and a mob event's reported amount both land here as
    // "what left the bar", which is also what the QA log prints.
    double amount = 0.0;
    bool died = false;    // the alive -> dead EDGE happened on this call
    bool applied = false; // false when the hit was discarded (the player is dead)
};

// THE single damage entry (card §2.1). Every source goes through it: the tick
// feeds it the fall damage the physics step produced, the actor events, and any
// source a later card adds.
//
// Clamping to [0, kMaxHealth] is why a 30-block fall can no longer leave the
// player at -7 HP, and the early return is why a mob that keeps hitting a
// corpse is harmless: `died` is raised once, on the crossing, and the later
// hits are discarded WITHOUT moving the health (a corpse stays at 0).
[[nodiscard]] inline DamageResult apply_damage(double &health, PlayerLife &life, const double amount,
                                               const glm::dvec3 &at) {
    if (life.dead) {
        health = std::clamp(health, 0.0, kMaxHealth);
        return {health, 0.0, false, false};
    }
    const double before = health;
    health = std::clamp(health - amount, 0.0, kMaxHealth);
    const double subtracted = before - health;
    if (health > 0.0) {
        return {health, subtracted, false, true};
    }
    life.dead = true;
    life.death_pos = at;
    return {health, subtracted, true, true};
}

// What a death drop did.
struct DropResult {
    int dropped = 0; // stacks that left the inventory (one drop each)
    int refused = 0; // stacks the authority would not take; they stay in the cell
};

// The death drop (card §2.3). ⚖ docs/01 §7: 死亡掉落全部物品与经验 - the items
// half is this card's, the experience half is cut (no experience system exists).
//
// One DropItems request per NON-EMPTY cell of the 41 (9 hotbar + 27 main + 4
// armour + 1 offhand), and a cell is cleared only after the authority accepted
// its request - the T-A1 ordering that keeps a refusal from eating an item.
// The drop itself (gravity, drag, merge, despawn) is the authority's existing
// item plane: spawn_item_stack_at() is already correct and this must not become
// a second copy of it (T-E1 owns it; the T-D40 lesson about duplicated motion).
//
// ⚠ The scattering details are 待校准 (card §2.3): every stack appears at the
// same point with no initial velocity, because the sources for a real scatter
// (spread radius, per-drop impulse) do not exist. 依据 docs/research/11 §8.1
// 「无来源的数值一律不写」.
[[nodiscard]] inline DropResult drop_inventory(game::Inventory &inventory, game::IAuthority &authority,
                                               const game::ActorPose &pose) {
    DropResult result;
    for (int slot = 0; slot < game::kInventorySlots; ++slot) {
        const game::ItemStack stack = inventory.slot(slot);
        if (stack.empty()) {
            continue;
        }
        game::ActionRequest req;
        req.kind = game::ActionKind::DropItems;
        // The request has no free-form payload; the item goes in item_or_block
        // and the count borrows target.x, exactly as PickUp borrows target.x for
        // an entity id (game/protocol.hpp, ActionKind::DropItems).
        req.target = {stack.count, 0, 0};
        req.item_or_block = stack.item;
        req.actor = pose;
        if (!authority.submit(req).accepted) {
            ++result.refused;
            continue; // still in the cell: the player keeps it (until the respawn)
        }
        inventory.set_slot(slot, game::ItemStack{});
        ++result.dropped;
    }
    return result;
}

// One damage instance and everything that follows from it - the call the logic
// tick makes for each hit. `pose` is the actor pose the drops are spawned at
// (the same one the tick sends the authority every tick); `at` is where the
// damage came from, which is what the corpse records.
struct DamageResolution {
    DamageResult damage;
    DropResult drop;
};

[[nodiscard]] inline DamageResolution take_damage(game::Inventory &inventory, game::IAuthority &authority,
                                                  const GameRules &rules, double &health, PlayerLife &life,
                                                  const double amount, const glm::dvec3 &at,
                                                  const game::ActorPose &pose) {
    DamageResolution resolution;
    resolution.damage = apply_damage(health, life, amount, at);
    if (!resolution.damage.died) {
        return resolution; // not the crossing: no drop, no inventory change
    }
    if (rules.keep_inventory) {
        // ⚖ the one gamerule this card carries: the stacks stay in the cells.
        return resolution;
    }
    // The drop lands where the player died, not where the damage came from: a
    // mob can hit from three blocks away and the items must not appear there.
    game::ActorPose drop_pose = pose;
    drop_pose.feet = life.death_pos;
    resolution.drop = drop_inventory(inventory, authority, drop_pose);
    return resolution;
}

// The FALL path's form of the above (card §2.1's first bullet). The physics step
// has already applied ⚖ floor(d − 3) to the state itself - its own tests read
// that number straight off the PlayerState, and physics_config's threshold is
// frozen - so this takes the difference the step produced, puts the health back
// and feeds it through the ONE entry. `health_before` is the value read before
// step_player ran. A step that dealt nothing (or healed, which nothing does
// yet) reconciles to no damage and reports `applied == false`.
[[nodiscard]] inline DamageResolution take_fall_damage(game::Inventory &inventory, game::IAuthority &authority,
                                                       const GameRules &rules, double &health, PlayerLife &life,
                                                       const double health_before, const game::ActorPose &pose) {
    const double fall_damage = std::max(0.0, health_before - health);
    health = health_before;
    if (fall_damage <= 0.0) {
        return {DamageResult{health, 0.0, false, false}, DropResult{}};
    }
    return take_damage(inventory, authority, rules, health, life, fall_damage, pose.feet, pose);
}

// The respawn action (card §5.6): back at the respawn point, at full health,
// standing still, with the fall arc and the death state cleared. The inventory
// is NOT touched here - the death drop owns it, and the load-time guard below
// runs before the player has one.
//
// The velocity is zeroed rather than kept: the corpse may have been falling or
// flying when it died, and a respawn that inherited that would throw the player
// out of the spawn point. `pose` is left alone - the physics step reads it from
// the input every tick, so the first tick after a respawn sets it correctly.
inline void respawn_player(physics::PlayerState &state, PlayerLife &life) {
    state.position = life.respawn_pos;
    state.velocity = glm::dvec3{0.0, 0.0, 0.0};
    state.fall_distance = 0.0;
    state.fall_peak_y = life.respawn_pos.y;
    state.on_ground = true;
    state.health = kMaxHealth;
    state.sprinting = false;
    state.sprint_toggle_timer = 0;
    state.collided_horizontally = false;
    life.dead = false;
    life.death_pos = glm::dvec3{0.0, 0.0, 0.0};
}

// What a respawn does to the inventory: 空手 with the rule off (the default),
// untouched with it on (card §2.6 - 含义：true 时死亡保留物品). A named function
// because it is the only other place the gamerule is read.
inline void respawn_clear_inventory(game::Inventory &inventory, const GameRules &rules) {
    if (rules.keep_inventory) {
        return;
    }
    for (int slot = 0; slot < game::kInventorySlots; ++slot) {
        inventory.set_slot(slot, game::ItemStack{});
    }
}

// ── what a dead player's tick may do (card §2.5) ────────────────────────────
// The tick's whole interaction half - input mapping, the physics step (hence
// movement), targeting, mining, placement, pick-up and the hotbar keys - hangs
// off this ONE predicate. "A dead player cannot dig, place, attack or move" is
// therefore enforced in one place instead of by a guard per verb, which is how
// a verb added next card ends up reachable from a corpse. The tick itself cannot
// run headless (it polls a real window), so the rule is a function a test can
// drive rather than an `if` buried in the tick; the on-machine check is that a
// dead player's clicks and WASD do nothing at all.
[[nodiscard]] constexpr bool may_act(const PlayerLife &life) {
    return !life.dead;
}

// ── the persisted respawn point (card §2.4) ─────────────────────────────────
// `spawn_x/y/z` is a field the level format has carried since T009
// (storage/level_file.hpp). What was wrong is what the client put in it: every
// save wrote the PLAYER's position, so the field drifted with the player and a
// respawn built on it would send a player who died far from home back to
// wherever they last quit. The three helpers below are the whole of the fix -
// write the respawn point, read it back, and pick between it and a fresh scan.
inline void write_respawn_point(storage::LevelData &level, const PlayerLife &life) {
    level.spawn_x = life.respawn_pos.x;
    level.spawn_y = life.respawn_pos.y;
    level.spawn_z = life.respawn_pos.z;
}

// Which point a session starts with: the saved one when the save carries a
// usable one, otherwise the freshly scanned spawn column. A `spawn_y <= 0` save
// is not usable - a find_spawn() column always stands on a surface above the
// void, and a zeroed field is what a level written before T-D45 with a player
// at y<=0 (or a truncated one) looks like; respawning at the world origin would
// drop the player into the ground at (0, 0, 0).
[[nodiscard]] inline glm::dvec3 choose_respawn_point(const bool have_save, const storage::LevelData &level,
                                                     const glm::dvec3 &scanned) {
    if (have_save && level.spawn_y > 0.0) {
        return {level.spawn_x, level.spawn_y, level.spawn_z};
    }
    return scanned;
}

// A save whose stored health is <= 0 is a death that was never resolved: before
// this card the client persisted the 0 and restored it verbatim, so reloading
// such a save started the session as a corpse with no way out (nothing heals).
// The load path treats it as a respawn instead of restoring the corpse.
[[nodiscard]] constexpr bool save_needs_respawn(const double stored_health) {
    return stored_health <= 0.0;
}

} // namespace opencraft::client
