#pragma once

// T-A1: the request/result vocabulary of the authoritative split, shared by
// both ends (docs/03 §1: game/common carries the 协议消息定义).
//
// Everything here is plain value data - no pointers, no callbacks, no shared
// mutable state - so the in-process channel this card builds can be replaced by
// a serializing network channel in M3 without touching the call sites.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace opencraft::game {

// ⚖ docs/01 §4: survival block reach, in blocks. One copy for both ends: the
// client aims with it and the authoritative side re-checks it (T-A1).
inline constexpr double kReachDistance = 4.5;

// ⚖ research/11 §1.5.1: 玩家近战到达距离 3 格 (creative mode with a non-spear item
// is 5). One copy for both ends, exactly like kReachDistance: the client picks its
// target with it and the authority re-checks it against its own entity boxes.
inline constexpr double kAttackReach = 3.0;

// ⚖ A bare-handed hit does 1 damage. This is what an item with no weapon
// numbers uses: ItemDef::attack_damage is 0.0 for everything that is not a
// tool, and that 0.0 means "use this" (item_registry.hpp's attack_damage_of).
// T-D46 added what surrounds this number (the hurt window, armour and the base
// knockback); T-D59 added the swing model below - the charge ramp, the crit and
// the sprint bonus - so the bare hand now scales with the same ramp as a sword,
// it just has no reach past this floor when it swings cold.
inline constexpr double kPunchDamage = 1.0;

// ⚖ research/01 §6.1: 徒手/其他 攻速 4.0 ⇒ T = 5 ticks. The partner of
// kPunchDamage above and used the same way (ItemDef::attack_speed 0.0 ⇒ this).
inline constexpr double kPunchAttackSpeed = 4.0;

// ── T-D59: the swing model (research/01 §6.1/§6.2/§6.4) ─────────────────────
// These live here rather than in either end's own headers because both ends
// need the SAME numbers: the client draws the charge bar from them, the
// authority settles the damage from them, and M3 puts both on the wire. They
// sit next to the reach and the punch damage for the same reason those do -
// one copy, so the two ends cannot disagree about a game rule.

// One ⚖ second of charge, in ticks.
inline constexpr double kTicksPerSecond = 20.0;

// ⚖ research/01 §6.1: multiplier = clamp(0.2 + ((t+0.5)/T)² × 0.8, 0.2, 1), with
// T = 20/attack_speed and t the ticks since the last swing. The squared ratio is
// INSIDE the ×0.8 (§6.1's own line; the "(t+0.5)/T²" spelling further down that
// document is shorthand for the same thing - T-D59's card calls it out).
//
// The floor is what makes a fast click rhythm a choice rather than a free win:
// a swing with no charge at all still does 20%.
inline constexpr double kAttackChargeFloor = 0.2;
// ... and the ramp supplies the remaining 0.8, reaching all of it only at t = T.
inline constexpr double kAttackChargeSwing = 0.8;

// ⚖ research/01 §6.1/§6.2/§6.4: the 84.8% charge that unlocks the crit and the
// sprint knockback. It is a THRESHOLD, not the point where the damage reaches
// full - that is t = T (docs/01 §4's "84.8% 冷却达到全额伤害" wording, which
// T-D59 corrects). A fist (T = 5) reaches exactly this at t = 4, which is why
// the boundary is asserted on both sides.
inline constexpr double kAttackChargeThreshold = 0.848;

// ⚖ research/01 §6.2: 下落中攻击 + 冷却 ≥84.8% ⇒ 伤害 ×1.5. The conditions the
// base game also excludes (in water, climbing, slow falling, riding, flying)
// have no systems behind them in OpenCraft yet, so they cannot be honoured -
// see the ruling's boundary note rather than inventing a test for them.
inline constexpr double kCritDamageMultiplier = 1.5;

// ⚖ research/01 §6.4: 疾跑命中额外 +0.5 水平, gated on the same 84.8%. The base
// 0.4 the sum is built on is world_sim.cpp's kAttackKnockback (the mobs' half)
// and player_life.hpp's kKnockbackSpeed (the player's half).
inline constexpr double kSprintKnockbackBonus = 0.5;

// What a swing is worth after `ticks_since_last` ticks, for a weapon whose
// attack speed is `attack_speed` (always the EFFECTIVE one - ItemDef's 0.0 has
// already been resolved to kPunchAttackSpeed by item_registry's accessor).
//
// t is an INTEGER tick count and T is a double: a pick's T is 16.666…, and
// rounding it to 16 or 17 would move every point of its ramp.
[[nodiscard]] inline double attack_charge_multiplier(const double ticks_since_last, const double attack_speed) {
    const double period = kTicksPerSecond / attack_speed;
    const double progress = (ticks_since_last + 0.5) / period;
    return std::clamp(kAttackChargeFloor + progress * progress * kAttackChargeSwing, kAttackChargeFloor, 1.0);
}

// What the client asks the authoritative side to do - one entry per
// world-changing player action in T-A1's scope.
//
// The inventory is deliberately not in this vocabulary: a request carries the
// block to place (the T-A1 card's sketch does the same), so the authority
// decides the world outcome from its own data only. Moving the inventory across
// the channel belongs to M3, together with the player/session.
enum class ActionKind : std::uint8_t {
    Dig,        // break the block at `target` into air
    PlaceBlock, // write `item_or_block` at `target`
    PourWater,  // place a water source at `target`
    ScoopWater, // remove the water source at `target`
    // T-E1: hand one dropped item stack to the actor. `target.x` carries the
    // ENTITY id of the drop and `item_or_block` the item id the client read
    // from its own view; the authority re-checks both, so a stale entity id
    // (the store reuses slots) can never hand over a different item. The other
    // two target components are unused.
    //
    // This is the one verb whose effect lands on the client's side of the
    // split: the authority owns the drop and the pickup rules, while the
    // inventory is still the client's (T-A1 kept it out of this vocabulary on
    // purpose; M3 moves it). The client reads the stack WHILE the drop still
    // holds it - the same read-then-spend ordering place_one_block uses - and
    // only asks once its own dry run says the stack fits.
    PickUp,
    // ── T-M2 mobs (appended; every earlier verb keeps its meaning) ─────────
    // Hit the mob whose entity id is in `target.x`. `target.y/z` unused; the
    // authority re-checks the reach (research/11 §1.5.1: 玩家近战到达距离 3 格)
    // and the mob's own collision box, so the client cannot hit from further
    // away than it can see.
    //
    // This is the minimum damage channel that makes the passive roster's drops
    // REACHABLE: the card asks for a passive mob that 掉落食物, and a loot table
    // nothing can trigger would be the "implemented but unreachable" pattern
    // this project keeps paying for. One request is ONE swing: the client sends
    // it on the rising edge of the left button, so its rate is the player's, not
    // the tick loop's.
    //
    // T-D46 added the base knockback and the victim-side window/armour (the
    // victim's armour lives on the client, where the player's hit points are).
    // T-D59 completed the swing: the damage is now the held item's, scaled by
    // the charge ramp, with the crit and the sprint bonus at the 84.8%
    // threshold. The authority keeps the charge clock itself (last_attack_tick_)
    // and resolves the weapon from `actor.held_item`, so the client sends no
    // amount and no timing - the numbers above are rules, not payload.
    Attack,
    // Hand one unit of the held food to the mob whose entity id is in
    // `target.x`; `item_or_block` carries the item id the client believes it is
    // holding, re-checked here for the same reason PickUp re-checks its item.
    // The authority decides whether that item tempts that species, whether the
    // mob is an adult, and whether its breeding cooldown has elapsed.
    Feed,
    // ── T-D45 death drop (appended; every earlier verb keeps its meaning) ────
    // Turn ONE inventory stack into an ordinary world drop. ⚖ docs/01 §7: 死亡
    // 掉落全部物品与经验 - the client sends one of these per non-empty cell of
    // its 41, then clears that cell.
    //
    // Field reuse (the request has no free-form payload, and this card may only
    // APPEND to the enum): `item_or_block` carries the item id, `target.x` the
    // stack count, `actor.feet` where the drop appears (the death position).
    // Same shape as PickUp, which borrows `target.x` for an entity id.
    //
    // The drop itself is spawned by the authority through the SAME
    // spawn_item_stack_at() a broken block and a dead mob use - the point of the
    // verb is that the client does not get a second copy of the drop physics
    // (T-E1 owns it, research/11 §4.1). The authority re-checks the item id, the
    // count and that the destination chunk is resident; it does NOT re-check the
    // actor's reach, because dying is not a reachable-distance action.
    DropItems,
};

// The actor geometry the authority validates against. Value data on purpose:
// over a network these are the fields the client claims and the server
// re-checks against its own copy of the player.
struct ActorPose {
    glm::dvec3 feet{0.0, 0.0, 0.0}; // feet centre, same convention as PlayerState::position
    double height = 1.8;            // current pose height (1.8 standing / 1.5 sneaking)
    double eye_height = 1.62;       // eye above the feet; the authority derives the eye itself
    // What the actor is holding (T-M2). It is part of the pose rather than of a
    // request because the mob rules need it PER TICK and without an action: the
    // tempt goal exists precisely when the player is holding the breeding food
    // and is NOT clicking anything. The inventory itself is still the client's
    // (T-A1); this is the one item id the authority is told about, and it is
    // re-checked wherever it leads to a world change (the Feed verb).
    //
    // T-D59 gave the two attack rules that read it their fields: the swing's
    // reach AND its damage and speed come from this item.
    std::uint16_t held_item = 0;

    // ── T-D59 attack qualifiers (the two fields above keep their meaning) ────
    // Both are DECLARED BY THE CLIENT and used by the authority exactly where
    // held_item is: at the point they change the world, and nowhere else. The
    // authority never trusts them as a description of itself - it trusts them
    // the way it trusts `feet`, i.e. as the claim the client makes about its own
    // body, which M3 validates against the server's copy instead of believing.
    // Until then the split is the same one PlayerState has always had (T-A1
    // ruling C-1: the player is the client's until M3).
    //
    // ⚖ research/01 §6.2: the crit needs "下落中" and §6.4's sprint bonus needs
    // "疾跑中".
    bool sprinting = false;
    // FALLING is defined, not inferred twice: `!on_ground && velocity.y < 0.0`.
    // The sign matters - a player rising through a jump is not falling, so a
    // hit on the way up is not a crit (the base game's own "falling > 0").
    bool falling = false;
};

struct ActionRequest {
    ActionKind kind = ActionKind::Dig;
    glm::ivec3 target{0, 0, 0};
    std::uint16_t item_or_block = 0; // PlaceBlock: block id of the held stack
    ActorPose actor{};
    std::uint32_t sequence = 0; // reserved for M3 prediction/reconciliation; unused in this card
};

// Why a request was refused. A stable code rather than a prose message: the
// tests match on it and M3 ships it to the player as feedback (the prose comes
// from action_reject_reason()).
enum class ActionReject : std::uint8_t {
    None = 0,        // accepted
    OutOfWorld,      // target y outside [0, Chunk::kSizeY)
    ChunkNotLoaded,  // the target's chunk is not in memory: the write would be dropped
    OutOfReach,      // target outside the actor's ⚖ reach (kReachDistance)
    NothingToDig,    // Dig on air
    Unbreakable,     // Dig on a block with hardness < 0 (bedrock)
    UnknownBlock,    // PlaceBlock with an id the registry does not know, or with air
    CellOccupied,    // PlaceBlock / PourWater into a cell that is not replaceable
    IntersectsActor, // PlaceBlock into the actor's own AABB
    NotAWaterSource, // ScoopWater where there is no source
    // ── T-E1 PickUp (appended; every earlier code keeps its value) ──────────
    UnknownEntity,      // no live entity with that id
    EntityItemMismatch, // the entity holds a different item than the client claims
    PickupDelayActive,  // the drop's ⚖ pickup delay (10 ticks natural) has not elapsed
    OutOfPickupRange,   // outside the actor's pick-up box (game/pickup.hpp)
    EntityNotLoaded,    // the drop's chunk is not in memory: its timers are paused
    // ── T-M2 Attack / Feed (appended; every earlier code keeps its value) ────
    NotAMob,          // the entity exists but is not a mob (a drop is not a target)
    OutOfAttackRange, // outside the player's melee reach (research/11 §1.5.1)
    NotBreedable,     // that mob species is never tempted (no breeding food)
    WrongFood,        // the held item is not what tempts this species
    MobNotAdult,      // a baby cannot breed
    BreedingCooldown, // the mob is still on its ⚖ 5-minute breeding cooldown
    // ── T-D45 DropItems (appended; every earlier code keeps its value) ───────
    UnknownItem,   // no item with that id in the registry (or the empty id)
    BadStackCount, // stack count outside [1, that item's max_stack]
};

[[nodiscard]] const char *action_reject_reason(ActionReject reject);

struct ActionResult {
    bool accepted = false;
    ActionReject reject = ActionReject::None;

    // Human-readable form of `reject`; "" while accepted.
    [[nodiscard]] const char *reason() const { return action_reject_reason(reject); }
};

// What happened to the player, in the one place both ends can name it.
enum class ActorEventKind : std::uint8_t {
    MeleeHit = 0, // a mob's melee attack landed on the player
    Explosion,    // a mob detonated (amount is the blast damage at the player)
};

// Something the simulation did TO the player, worth telling the client about
// (T-M2). Value data, so it survives the M3 wire unchanged.
//
// The player's hit points live on the client (T-A1 kept the player out of this
// vocabulary: 库存/血量上收 is M3 work), so a mob cannot subtract them where the
// mobs are simulated. Instead the authority reports the event and the client,
// which owns the health it renders, applies it. That asymmetry is temporary and
// deliberate - it is the same split T-E1 used for PickUp, pointing the other
// way.
struct ActorEvent {
    ActorEventKind kind = ActorEventKind::MeleeHit;
    glm::dvec3 position{0.0, 0.0, 0.0}; // where the damage came FROM: the attacker's feet (Melee,
                                        // T-D46) or the blast centre (Explosion) - the client pushes the
                                        // victim away from this point
    double amount = 0.0;                // damage to apply, already difficulty-scaled
    std::uint16_t source_type = 0;      // the mob's entity type id (for feedback)
};

// Authoritative -> client push-back, drained once per tick: the derived work
// the client must redo because the authority changed something.
//
// It carries no block deltas on purpose. In-process the client renders the
// authority's own storage through a read-only view (client::WorldSource), so
// block and fluid *state* needs no shipping - only the "what is stale now"
// list does. The day a copy exists on the client (M3, with prediction), the
// same struct grows the per-block events.
struct WorldChanges {
    // Chunks whose baked mesh is stale (own chunk + light/fluid neighbors).
    std::vector<std::pair<int, int>> dirty_chunks;

    // T-M2: things that happened to the player this tick. Appended next to the
    // dirty list rather than in it, because the two are consumed by different
    // code (meshing vs the client's own health) and one is a mesh hint while
    // the other is a game rule.
    std::vector<ActorEvent> actor_events;

    void clear() {
        dirty_chunks.clear();
        actor_events.clear();
    }

    [[nodiscard]] bool empty() const { return dirty_chunks.empty() && actor_events.empty(); }
};

// ── world streaming (T-D4) ──────────────────────────────────────────────────
// Where the viewer is and how much work one streaming call may spend. Chunk
// coordinates and ints, not a float world position: both ends already work in
// chunk space, and integers cross a network hop without a rounding argument.
//
// The two radii are the policy, named by the caller and enforced by the
// authority:
//   * the chunks within `generate_radius` (Chebyshev distance, in chunks) are
//     the working set - generated on demand, up to the caller's budget;
//   * a chunk is released only once it is further away than `unload_radius`.
// The gap between them is the hysteresis that keeps a player pacing over the
// edge of the view from loading and unloading the same chunk every step (and
// re-writing its dirty data every step). `unload_radius == generate_radius` is
// the zero-hysteresis configuration.
struct StreamRequest {
    int center_cx = 0;
    int center_cz = 0;
    int generate_radius = 0; // working set: chunks, Chebyshev distance
    int unload_radius = 0;   // release threshold; >= generate_radius
    int generate_budget = 0; // how many chunks this call may create (frame budget)
    // The persistence window is due: besides streaming, run the authority's own
    // write-back of every dirty loaded chunk (the T009 cadence pass).
    bool persist = false;
    // A radius of 0 means "just the centre" / "release beyond the centre", so a
    // caller that wants streaming must send the windows it means - the client
    // builds them from its config (client::make_stream_request).
};

struct StreamResult {
    // Chunks this call created, nearest-first. The client can pace its own
    // derived work (meshing) with them; nothing else about them is stale.
    std::vector<std::pair<int, int>> loaded_chunks;
    // Chunks this call released, ascending. Their world data is gone - the
    // dirty ones were written to the save first - so the client drops whatever
    // it owns for them; the GPU mesh is the one that matters (渲染资源属客户端).
    std::vector<std::pair<int, int>> unloaded_chunks;
    std::size_t persisted_chunks = 0; // chunks handed to the save by this call
    std::size_t loaded_total = 0;     // chunks in the authority's memory afterwards
};

// The client's handle on the authoritative side - the whole of it. Everything
// the client is allowed to do to the world goes through these five verbs;
// server::WorldSim is the in-process implementation, and M3 swaps in one that
// serializes `submit()` onto a socket (its world already lives elsewhere, so
// the others become message pumps).
class IAuthority {
public:
    virtual ~IAuthority() = default;

    // Executes one request, or refuses it with a reason. The world write (if
    // any) has already happened when this returns.
    [[nodiscard]] virtual ActionResult submit(const ActionRequest &req) = 0;

    // One authoritative simulation step (the fluid scheduled ticks). The
    // authority runs inside the client process for now, so the client's fixed
    // step drives it; a standalone server owns its own 20 TPS loop in M3.
    virtual void tick() = 0;

    // Takes everything changed since the previous call; the authority's buffer
    // is emptied, so each change is reported exactly once.
    [[nodiscard]] virtual WorldChanges take_changes() = 0;

    // Persistence pump (T009 cadence): hands the attached save a snapshot of
    // every dirty chunk and returns how many were queued. Not a world write -
    // it is the authority's own maintenance. T-D4 stopped the client from
    // driving it directly (it is world management, not a client verb): the
    // client asks for it through stream()'s `persist` field, which runs exactly
    // this pass. The method stays on the interface because it is what both ends
    // mean by "write back what is dirty".
    virtual std::size_t autosave_pass() = 0;

    // Streaming maintenance in request form (T-D4): the client names where the
    // viewer is and what the call may spend; the authority decides what to
    // generate, what to release - dirty chunks written to the save first - and,
    // when the window is due, what to write back. The client no longer drives
    // chunk loading, chunk releasing or the autosave pass itself: those are
    // world management, and the world belongs to the authority.
    [[nodiscard]] virtual StreamResult stream(const StreamRequest &req) = 0;

    // Where the player is, for the systems that have to notice them (T-M2).
    //
    // The five verbs above are all EVENT driven - they happen when the player
    // does something. Mob AI is not: a mob has to see the player every tick it
    // is awake, whether or not the player pressed anything, so the authority
    // needs the viewer's pose as a per-tick input. The client already computes
    // exactly this on every logic tick (it is the same ActorPose it attaches to
    // each ActionRequest), and M3's player-input packet carries the same fields
    // - so this verb is where the network's "player state" message will land.
    //
    // It has a default empty body, so an IAuthority that has no mobs to feed
    // does not have to care: nothing happens to a world whose mobs never learn
    // where the player is.
    virtual void observe_actor(const ActorPose &pose) { (void) pose; }
};

} // namespace opencraft::game
