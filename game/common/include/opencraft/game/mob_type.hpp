#pragma once

// T-M2: the mob CONTENT layer - one table entry per mob, and the registry that
// turns it into (a) an EntityDef in the entity type registry, so a mob is
// stepped by the same physics and the same collision as everything else
// (docs/03 §6: 物理参数按实体类型实例化), and (b) a MobDef of AI content.
//
// ── THE POINT OF THIS FILE ──────────────────────────────────────────────────
// The card's real deliverable is "目标栈框架 + 可扩展的 EntityDef 填表机制", with
// three mobs as the samples that prove it. So this file is written to be the
// answer to "再新增一种生物需要改哪些地方": one function per mob, and one line in
// create_default() that registers it. Nothing else in the codebase needs to
// change - no switch, no enum, no factory:
//
//   * the AI itself is content-free (server/sim/mob_sim.hpp reads MobDef),
//   * the goal stack is content-free (game/mob_goal.hpp schedules whatever
//     table the MobDef carries),
//   * the spawn rules are content-free (server/sim/mob_spawn.hpp reads
//     MobClass and the shell size; the caps are per CLASS, not per mob),
//   * the drops are resolved from string ids at startup, so a typo throws
//     immediately instead of silently dropping nothing.
//
// ── THREE DISTANCES, THREE FIELDS (research/11 §1.3.1) ──────────────────────
// The single easiest way to get mob behaviour visibly wrong is to conflate
// these, so they are separate fields with separate names and separate
// defaults, and each has a unit test that moves it and asserts the behaviour
// changed:
//
//   sight_range      察觉距离   - how far the mob NOTICES the player   (16)
//   follow_range     放弃追踪   - how far it keeps chasing a LOCKED target
//                                 (attribute default 32; zombie-line 35)
//   tempt_range      引诱距离   - how far it senses a player holding the
//                                 breeding food (attribute 10)
//
// ⚠ research/11 §1.3.1 records a conflict in the source itself: the attribute
// infobox gives follow_range = 32 while the same table's "everything else" row
// says 16, and the Mob page says most mobs notice players within 16. Those are
// DIFFERENT quantities (give-up distance vs notice distance), so this file
// keeps both and sets them per mob. Where a mob's own value is not in the
// sources, the field carries the attribute default and says so.
//
// ⚠ The tempt triple is deliberately more than one field, because the source
// distinguishes the sensing radius from the behaviour thresholds (research/11
// §1.5.6): the attribute is 10, but a cow actually STARTS following at 6 and
// STOPS at ≥10. Squashing those into one number makes a cow either never
// approach or never leave.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "opencraft/game/entity_type.hpp"
#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/mob_goal.hpp"

namespace opencraft::game {

// Difficulty (docs/01 §7: 和平/简单/普通/困难). It is the input to the damage
// lookup and the switch that keeps hostile mobs out of a peaceful world.
enum class Difficulty : std::uint8_t {
    Peaceful = 0,
    Easy = 1,
    Normal = 2,
    Hard = 3,
};

// Which spawn cap a mob counts against and whether the light rule applies
// (docs/01 §6: 敌对生成光照等级 0；被动生物按区块一次性生成).
enum class MobClass : std::uint8_t {
    Passive = 0,
    Hostile = 1,
};

// One loot row: an item and an inclusive count range. The item is resolved from
// a string id at registration; the count is rolled from the mob's own
// deterministic RNG (mob_spawn.hpp), so a kill is reproducible.
struct MobDrop {
    std::uint16_t item = 0;
    int min_count = 0;
    int max_count = 0;
};

// Everything about one mob that is not its per-instance state.
struct MobDef {
    // ── physics (docs/03 §6: 物理参数按实体类型实例化) ──────────────────────
    // The same struct a dropped item uses, registered into the same
    // EntityTypeRegistry. research/11 §4.1 is why a mob cannot share the drop's
    // numbers: mobs move P→A→D with gravity 0.08 and horizontal drag 0.91,
    // drops move A→P→D with 0.04 and 0.98, so one flat config cannot express
    // both. The drop's box is 0.25³, a cow's is 0.9×1.4.
    EntityDef physics;

    MobClass mob_class = MobClass::Passive;

    // ── locomotion ──────────────────────────────────────────────────────────
    // ⚖ research/01 §10.2 gives each mob a movement speed ATTRIBUTE, and
    // research/11 §0.1 states the unit is the block/tick order of magnitude -
    // so 0.23 is 0.23 blocks/tick = 4.6 m/s. Cross-checked against the one
    // speed this repository has measured: the player walks 4.317 m/s = 0.216
    // blocks/tick, so a zombie at 0.23 is ~6% faster than a walking player and
    // clearly slower than a sprinting one - which is exactly how the pair
    // behaves in the base game. ⚠ The sources never spell the conversion out;
    // it is asserted in the tests and flagged in the report as an
    // interpretation, not as a quoted number.
    double move_speed = 0.2;

    // ── the three distances (see the header comment) ────────────────────────
    double sight_range = 16.0;
    double follow_range = 32.0;
    double tempt_range = 0.0; // 0 = this mob is never tempted
    double tempt_start_range = 6.0;
    double tempt_stop_range = 10.0;
    std::uint16_t tempt_item = 0; // the held item that tempts, resolved at registration

    // ── breeding (research/11 §6.2) ─────────────────────────────────────────
    // ⚖ 求偶双方互相寻路靠近，最远 8 格; 冷却 5 分钟; 交配约 2.5 秒;
    // 幼体成长 24000 tick.
    double breed_range = 8.0;
    int love_ticks = 50;
    int breed_cooldown = 6000;
    int baby_growth_ticks = 24000;

    // ── strolling (research/11 §1.5.1) ──────────────────────────────────────
    // ⚖ 玩家在 32 格内才游荡. ⚠ The stroll radius itself has no source; it is
    // an implementation knob (10 blocks for an ordinary stroll), marked
    // 待校准 rather than presented as a quoted value.
    double stroll_trigger_range = 32.0;
    double stroll_radius = 10.0;

    // ── melee ───────────────────────────────────────────────────────────────
    // ⚖ research/01 §10.2 per difficulty, for the zombie-line mob: 2.5 / 3 /
    // 4.5. ⚠ docs/01 §6 states the ladder generically as 简单 ×1 / 普通 ×1.5 /
    // 困难 ×2, which does NOT reproduce the 简单 value (it would give 3, not
    // 2.5) - the source row wins here, and the discrepancy is in the report.
    double damage_easy = 0.0;
    double damage_normal = 0.0;
    double damage_hard = 0.0;
    // ⚖ research/11 §1.5.1: a mob's melee box is its collision box inflated by
    // ≈0.828 (√2.04 − 0.6) when it has no attack_range component, and the hit
    // additionally needs line of sight. Using the box (not a centre distance)
    // is the rule the card repeats.
    double attack_reach_extra = 0.828;
    // ⚠ No source for the melee attack INTERVAL (research/11 §1.5.1 gives the
    // player's sword swing speed, not the mob's). 20 ticks = one hit per second
    // is the calibration knob; marked, not quoted.
    int attack_cooldown = 20;

    // ── the exploder (research/11 §1.5.5) ───────────────────────────────────
    // ⚖ 玩家进入 3 格内且有视线 → 1.5 秒（30 tick）引信；保持视线拉开 7 格可取消。
    // fuse_ticks == 0 means "this mob does not explode".
    double fuse_trigger_range = 0.0;
    double fuse_cancel_range = 0.0;
    int fuse_ticks = 0;
    // ⚖ docs/01 §6: 爆炸威力 3 (TNT 等价 4). research/01 §10.2 gives the
    // epicentre damage: 普通 ~49, 困难 64.5. ⚠ The 简单 value is not in any
    // source; it is set equal to 普通 and marked 待校准 rather than invented.
    double explosion_power = 0.0;
    double explosion_damage_easy = 0.0;
    double explosion_damage_normal = 0.0;
    double explosion_damage_hard = 0.0;
    // ⚠ 待校准: the blast radius. No source in this repository states it; the
    // value is the base game's own 2 × power convention, recorded as a
    // calibration knob. Damage falls off linearly from the epicentre value to 0
    // at this radius, which is also ⚠ uncalibrated.
    double explosion_radius = 0.0;

    // ── panic / fleeing (research/11 §1.5.7, §6.3) ──────────────────────────
    // ⚖ 受伤反应: 随机方向逃跑数秒 (no tick count in the source ⇒ panic_ticks
    // is a knob, marked). ⚖ 逃跑检测周期 5 tick、逃跑目标距源 4–15 格 are both
    // sourced. ⚠★ 逃跑时的速度倍率: the wiki gives NO value (and the page is
    // still marked wip) - so this field is deliberately 1.0, i.e. "no boost
    // until calibrated". Inventing 1.4 here is exactly what T-D36 forbids.
    int panic_ticks = 60;
    double flee_speed_multiplier = 1.0;
    double flee_min_range = 4.0;
    double flee_max_range = 15.0;
    int flee_check_period = 5;

    // ── loot and xp ─────────────────────────────────────────────────────────
    std::vector<MobDrop> drops;
    // ⚖ research/01 §10.2 (kills award XP). The XP system does not exist yet
    // (M2c has its own card for it), so this is INTERFACE ONLY in this card -
    // the same arrangement T-E1 used for the thrown-item pickup delay.
    int xp_reward = 0;

    // ── the goal table ──────────────────────────────────────────────────────
    // Priorities follow research/11 §1.2.2's zombie table where it applies
    // (反击 1 / 主动选目标 2 / 攻击 2 / 游荡 7 / 环视 8). A goal that is absent
    // from this vector simply does not exist for the mob - which is how the
    // passive roster carries no TargetPlayer goal at all.
    std::vector<GoalEntry> goals;

    [[nodiscard]] bool is_explosive() const { return fuse_ticks > 0; }

    [[nodiscard]] bool is_breedable() const { return tempt_item != ItemRegistry::kEmptyId; }

    // ⚖ 难度倍率 (docs/01 §6): the melee damage for a difficulty.
    [[nodiscard]] double damage_for(Difficulty difficulty) const;
    // The epicentre explosion damage for a difficulty.
    [[nodiscard]] double explosion_damage_for(Difficulty difficulty) const;
    // Explosion damage at `distance` from the epicentre, before difficulty:
    // linear falloff from 1.0 at the centre to 0.0 at explosion_radius.
    // ⚠ 待校准 (see explosion_radius).
    [[nodiscard]] double explosion_falloff(double distance) const;

    // The mob's goal table in scan order (see sorted_goals).
    [[nodiscard]] std::vector<GoalEntry> ordered_goals() const { return sorted_goals(goals); }
};

// String-id to MobDef mapping, built once at startup next to the other two
// registries (docs/03 §7). Keyed by the ENTITY TYPE id, because that is what an
// Entity carries: given an entity in the store, `find(entity.type)` is the
// whole test for "is this a mob and what are its rules".
class MobRegistry {
public:
    MobRegistry() = default;

    // Registers the mob's physics into `types` and keeps its AI content.
    // Returns the entity type id. Throws std::invalid_argument on an empty or
    // duplicate id, on a def whose physics is not marked as a mob, and when a
    // content reference (tempting food, loot) names an unregistered item - the
    // same "fail at startup, not in the field" contract the other registries
    // use.
    std::uint16_t register_mob(EntityTypeRegistry &types, std::string id, MobDef def);

    // The launch roster. `types` and `items` are filled in by the caller
    // (server::WorldSim owns both), which is why this is a free function taking
    // them rather than a self-contained factory: the item id space belongs to
    // the authority (T-E1).
    [[nodiscard]] static MobRegistry create_default(EntityTypeRegistry &types, const ItemRegistry &items);

    // nullptr for every entity type that is not a mob - the single answer to
    // "is this entity a mob".
    [[nodiscard]] const MobDef *find(std::uint16_t entity_type) const;
    [[nodiscard]] const MobDef *find_by_id(std::string_view id) const;
    [[nodiscard]] std::uint16_t entity_type_of(std::string_view id) const;

    [[nodiscard]] bool is_mob(std::uint16_t entity_type) const { return find(entity_type) != nullptr; }

    [[nodiscard]] std::size_t size() const { return defs_.size(); }

    // Every mob's entity type id, ascending. Used by the spawner (which walks
    // the roster) so the spawn code holds no per-mob knowledge.
    [[nodiscard]] std::vector<std::uint16_t> entity_types(MobClass mob_class) const;

private:
    std::vector<MobDef> defs_;
    std::vector<std::uint16_t> entity_type_of_;
    std::unordered_map<std::uint16_t, std::size_t> index_by_entity_type_;
    std::unordered_map<std::string, std::size_t, StringHash, std::equal_to<>> index_by_id_;
};

} // namespace opencraft::game
