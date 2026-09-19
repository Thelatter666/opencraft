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
#include <cmath>
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

// ── T-D46: the combat rules (the player's side of them) ─────────────────────
// Three of them, all in this one place because they all decide the same thing:
// how much of an incoming hit reaches the hit points. What they do NOT do is
// touch the world - the armour is read from the inventory, the knockback is
// written into the physics state, and the authority is never asked (ruling C-1:
// the player stays the client's until M3).

// ⚖ research/11 §1.5.1: 受击后的无敌帧 10 tick - the same number the mobs use
// (MobRules::hurt_invulnerability, game/server/sim/mob_sim.hpp).
inline constexpr int kHurtInvulnerabilityTicks = 10;

// ⚖ research/01 §6.4: 基础攻击击退 - the horizontal INITIAL speed, blocks/tick.
// This is the PLAYER'S half: the shove a mob's landed hit gives the player, used
// by the ActorEvent branch in tick.cpp. It is the base value and nothing else -
// the sprint bonus is the PLAYER's own attack landing on a MOB, so it lives on
// the authority's side of the split (world_sim.cpp's kSprintKnockback: 0.4 + 0.5
// = 0.9, at the 84.8% charge), and a mob does not sprint. T-D59 wired that bonus
// up; it did not move this number, and this number must not grow to match it.
inline constexpr double kKnockbackSpeed = 0.4;

// What kind of damage is being applied. The distinction exists for exactly one
// rule - ⑤ / research/01 §2.3: fall, suffocation, void and starvation damage
// 绕过护甲 (armour does not reduce them).
//
// ⚠ Today only Melee, Explosion and Fall have producers (a mob's swings and
// blast, and the physics step's fall); the other three are named because
// contract ⑤ lists them, so this enum is the contract rather than a description
// of the current callers.
enum class DamageType : std::uint8_t {
    Melee = 0,
    Explosion,
    Fall,
    Suffocation,
    Void,
    Starvation,
};

// ⚖ research/01 §2.3: true when the armour formula does not apply at all.
[[nodiscard]] constexpr bool bypasses_armor(const DamageType type) {
    switch (type) {
    case DamageType::Fall:
    case DamageType::Suffocation:
    case DamageType::Void:
    case DamageType::Starvation:
        return true;
    case DamageType::Melee:
    case DamageType::Explosion:
        break;
    }
    return false;
}

// What the player is currently wearing, summed over the four armour cells.
struct ArmorTotals {
    double points = 0.0;
    double toughness = 0.0;
};

// Reads the four cells LIVE rather than caching a loadout total (card §7.5).
// The sum is four array reads and four registry lookups on a path that runs at
// most once per incoming hit - a mob's melee cooldown is 20 ticks - while a
// cache would have to be invalidated by every write to the inventory (the death
// drop, the respawn, the table at load, a future equip screen) and a stale one
// is a silent damage bug.
[[nodiscard]] inline ArmorTotals equipped_armor(const game::Inventory &inventory) {
    ArmorTotals totals;
    for (int piece = 0; piece < game::kArmorSlots; ++piece) {
        const game::ItemStack &stack = inventory.slot(game::armor_slot_index(static_cast<game::ArmorSlot>(piece)));
        if (stack.empty()) {
            continue;
        }
        const game::ItemDef &def = inventory.registry().def_of(stack.item);
        totals.points += def.armor_points;
        totals.toughness += def.armor_toughness;
    }
    return totals;
}

// ⚖ research/01 §2.1 (JE), transcribed exactly:
//
//     reduction = min(20, max(armor/5, armor - 4*damage/(min(toughness,20)+8))) / 25
//
// The outer 20 is the ⚖ 80% ceiling; the `armor/5` floor is where a huge hit
// lands, which is why the reduction FALLS as the damage grows (research/01
// §2.1's BreakingPoint: past `armor × (toughness + 8) / 5` the toughness term
// stops mattering).
[[nodiscard]] inline double armor_reduction(const double armor, const double toughness, const double damage) {
    const double points =
        std::min(20.0, std::max(armor / 5.0, armor - 4.0 * damage / (std::min(toughness, 20.0) + 8.0)));
    return points / 25.0;
}

// The damage that actually lands: ③'s formula, skipped entirely by ⑤'s exempt
// types. A non-positive amount comes back untouched - nothing heals through
// this path today, and scaling a heal by a damage formula would be nonsense.
[[nodiscard]] inline double damage_after_armor(const double damage, const DamageType type, const ArmorTotals &armor) {
    if (damage <= 0.0 || bypasses_armor(type)) {
        return damage;
    }
    return damage * (1.0 - armor_reduction(armor.points, armor.toughness, damage));
}

// ① The hurt window. ★ THIS IS damage_mob'S RULE, COPIED - see
// game/server/sim/mob_sim.hpp (`damage_mob`, plus MobAi::hurt_cooldown and
// last_hurt_amount for the mobs' copy of the state). The two live apart because
// the mobs are the authority's and the player is the client's (T-A1; ruling C-1
// keeps it that way until M3), NOT because they may drift: a change to one
// belongs in the other. Both callers also advance the counter once per tick in
// the same relative order (before the hits of that tick are settled).
//
// Returns what is left of `amount` after the window: 0 when the hit is immune,
// `amount - last_hurt_amount` when a bigger hit arrives inside the window, and
// `amount` itself when the window has closed.
[[nodiscard]] inline double settle_hurt(physics::PlayerState &state, const double amount) {
    if (state.invulnerability_ticks > 0 && amount <= state.last_hurt_amount) {
        return 0.0; // ⚖ 期间伤害 ≤ 原伤害则免疫
    }
    const double settled = state.invulnerability_ticks > 0 ? amount - state.last_hurt_amount : amount;
    state.last_hurt_amount = amount;
    state.invulnerability_ticks = kHurtInvulnerabilityTicks;
    return settled;
}

// One tick of the window, called once per logic tick by the live tick BEFORE
// that tick's hits are settled - the relative order step_mobs uses for the mobs
// (advance_timers runs before the goals). A hit on tick N therefore leaves the
// window open on the nine ticks after it and lets tick N+10 settle in full; the
// window is 10 ticks counting the tick the hit landed on, which is what the
// mobs' existing test measures too (it steps 11 ticks to be clear of it).
inline void tick_hurt_window(physics::PlayerState &state) {
    if (state.invulnerability_ticks > 0) {
        --state.invulnerability_ticks;
    }
}

// ⑥ The melee knockback, the player's half: ⚖ 0.4 blocks/tick of horizontal
// initial speed written into the victim's own velocity. The mobs' half is
// knock_back() in game/server/sim/mob_sim.hpp, called by server
// WorldSim::apply_attack - a second copy for the same reason the window above
// is one (the player is not an entity in the authority's store, ruling C-1).
// Keep the two in sync.
//
// `from` is where the hit came from (the attacker's feet / the blast centre).
// The impulse is ADDED to the velocity the player already carries and then
// decays through the physics step's own friction, so the number is a speed and
// not a distance (C-4 asks for the convention to be written down): the velocity
// is set HERE, at the end of the tick that received the hit, and the
// displacement happens on the NEXT tick's step_player, which reads the stored
// velocity as its starting point.
inline void push_away(physics::PlayerState &state, const glm::dvec3 &from, const double speed) {
    const double dx = state.position.x - from.x;
    const double dz = state.position.z - from.z;
    const double horizontal = std::sqrt(dx * dx + dz * dz);
    if (horizontal <= 1e-9 || speed <= 0.0) {
        return; // standing exactly on the attacker: there is no "away"
    }
    state.velocity.x += dx / horizontal * speed;
    state.velocity.z += dz / horizontal * speed;
}

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
    // T-D46 ⑤: the fall goes through the same reduction call as everything else,
    // with the type that BYPASSES armour (research/01 §2.3) - so "a fall is not
    // reduced by what you are wearing" is a table entry instead of an absent
    // call, and a test can tell the two apart. The amount is otherwise unchanged:
    // the ⚖ floor(d − 3) arithmetic and this path belong to T-D45.
    //
    // ⚠ The hurt window (①) is deliberately NOT applied here. It is the combat
    // channel's rule (contract ① is written against the mob-event path), and
    // adding it to the fall path would change the fall numbers T-D45 froze.
    const double landed = damage_after_armor(fall_damage, DamageType::Fall, equipped_armor(inventory));
    return take_damage(inventory, authority, rules, health, life, landed, pose.feet, pose);
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
