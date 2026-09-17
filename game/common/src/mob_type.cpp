#include "opencraft/game/mob_type.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace opencraft::game {

namespace {

// ── The launch roster: one function per mob ─────────────────────────────────
//
// Adding a mob is: write a function like these, add one line to
// create_default(). No switch, no enum, no factory - the AI, the goal stack
// scheduler and the spawner all read the data below.
//
// Names and looks are ORIGINAL (docs/04 red lines 2/5, docs/01 §6). None of
// them is a base-game mob name or a literal translation of one: the grazing
// bovine is a Mossback, the melee hunter is a Hollow Wretch, the walking bomb
// is a Blastbud. Their NUMBERS align with the ecological niche they fill
// (docs/01 §6: 数值对齐等价生态位), which is what the ⚖ comments record.

// The physics block every mob shares: the class the two stepping passes key off,
// plus research/11 §4.1's WALKING-BODY motion (gravity 0.08, drag 0.98 vertical /
// 0.91 horizontal) - deliberately not the drop's 0.04/0.98/0.98. One helper, so a
// new mob cannot register a body the drop pass would claim for its own.
[[nodiscard]] EntityDef mob_physics(const char *display_name, const double half_width, const double height,
                                    const int max_health) {
    EntityDef def;
    def.display_name = display_name;
    def.half_width = half_width;
    def.height = height;
    def.gravity = 0.08;
    def.vertical_drag = 0.98;
    def.horizontal_drag = 0.91;
    def.max_health = max_health;
    def.carries_item_stack = false;
    def.entity_class = EntityClass::Mob;
    return def;
}

// The grazing bovine - the passive sample. research/11 §6.1: HP 10, speed attr
// 0.2, body 1.4 tall x 0.9 wide, drops raw meat 1-3 plus hides 0-2, breeding
// cooldown 5 minutes, babies grow in 24000 ticks.
//
// ⚖ It also carries the three-distance lesson: it NOTICES the player within 16
// (the "most mobs" figure), would give up a locked target at the 32-block
// attribute default, senses the tempting food within the 10-block attribute -
// but actually starts following at 6 and stops at 10 (research/11 §1.5.6).
MobDef mossback_def(const ItemRegistry &items) {
    MobDef def;
    // ⚖ research/11 §6.1: body 1.4 tall x 0.9 wide, HP 10.
    def.physics = mob_physics("Mossback", 0.45, 1.4, 10);

    def.mob_class = MobClass::Passive;
    def.move_speed = 0.2; // ⚖ research/11 §6.1

    def.sight_range = 16.0;      // ⚖ 多数生物 16 格 (research/11 §1.3.1)
    def.follow_range = 32.0;     // ⚠ attribute default; see the wiki conflict note in mob_type.hpp
    def.tempt_range = 10.0;      // ⚖ attribute (research/11 §1.3.1)
    def.tempt_start_range = 6.0; // ⚖ 实际开始跟随 6 格 (research/11 §1.5.6)
    def.tempt_stop_range = 10.0; // ⚖ 牛 stops following at >=10 (research/11 §6.3)
    // The tempting food is the existing grain loaf (this project's bread/wheat
    // equivalent) rather than a new crop or a new item, so this card adds no
    // farming content. Which item tempts a species is OUR design; only the
    // distances above are aligned to the base game.
    //
    // It is also the one food the LAUNCH HOTBAR carries (slot 6), which is what
    // makes "walk up to a grazing mob and feed it" reachable in the live build -
    // the rest of the food sits in the main inventory, and the inventory screen is
    // a later card. A tempt item the player cannot hold is a rule that only exists
    // in tests.
    def.tempt_item = items.id_of("grain_loaf");

    def.breed_range = 8.0;         // ⚖ research/11 §6.2
    def.love_ticks = 50;           // ⚖ 交配约 2.5 秒
    def.breed_cooldown = 6000;     // ⚖ 5 分钟
    def.baby_growth_ticks = 24000; // ⚖ 20 分钟

    def.stroll_trigger_range = 32.0; // ⚖ 玩家 32 格内才游荡 (research/11 §1.5.1)
    def.stroll_radius = 10.0;        // ⚠ 无来源，实现旋钮（待校准）

    // research/11 §6.1: 生牛肉 1–3 + 皮革 0–2.
    def.drops = {{items.id_of("raw_haunch"), 1, 3}, {items.id_of("sturdy_hide"), 0, 2}};
    def.xp_reward = 2; // ⚖ 击杀 XP 1–3; interface-only in this card

    // Priorities: research/11 §1.2.3 gives the passive set (引诱 4 / 恐慌 1 /
    // 游荡 7 / 环视 8). 恐慌 outranks 引诱 because a hurt animal stops
    // following - the source says being attacked ends the tempt behaviour.
    def.goals = {
        {GoalKind::Panic, 1},        {GoalKind::Tempt, 4},        {GoalKind::Breed, 5},
        {GoalKind::RandomStroll, 7}, {GoalKind::LookAtPlayer, 8},
    };
    return def;
}

// The melee hunter - the hostile sample that proves the chase works.
// research/01 §10.2 zombie row: HP 20, damage 3 (简单 2.5 / 困难 4.5), speed
// attr 0.23, XP 5. research/11 §1.5.2: notices the player at 35 blocks and its
// follow_range attribute is 35 too.
MobDef hollow_wretch_def(const ItemRegistry &items) {
    MobDef def;
    // ⚠ 体形 source: research/11 §6.1 tabulates the PASSIVE bodies only and the
    // hostile rows in research/01 §10.2 carry no box. The width is the player's
    // 0.6 (this project's one humanoid body) and the height its 1.8; recorded as
    // an implementation choice, not a quoted value. HP 20 is ⚖ docs/01 §6.
    def.physics = mob_physics("Hollow Wretch", 0.3, 1.8, 20);

    def.mob_class = MobClass::Hostile;
    def.move_speed = 0.23; // ⚖ research/01 §10.2

    def.sight_range = 35.0; // ⚖ 僵尸察觉 35 (research/11 §1.3.1 / §1.5.2)
    // ⚖ The zombie-line follow_range attribute is 35 as well. That happens to
    // equal its sight range here, but the two are still separate fields - the
    // source warns explicitly that "察觉 = follow_range" must NOT be inferred
    // from this coincidence (research/11 §1.3.1).
    def.follow_range = 35.0;
    def.tempt_range = 0.0; // not temptable

    def.stroll_trigger_range = 32.0; // ⚖ research/11 §1.5.1
    def.stroll_radius = 20.0;        // ⚠ 大范围游荡 has no source radius (待校准)

    // ⚖ research/01 §10.2. See mob_type.hpp for why 简单 is 2.5 rather than the
    // 3 that docs/01 §6's generic ladder would produce.
    def.damage_easy = 2.5;
    def.damage_normal = 3.0;
    def.damage_hard = 4.5;

    def.panic_ticks = 0; // a hunter does not panic (no Panic goal below)
    // An original loot choice: a thing that lives in the dark carries coal.
    def.drops = {{items.id_of("char_lump"), 0, 1}};
    def.xp_reward = 5; // ⚖ research/01 §10.2

    def.goals = {
        {GoalKind::Revenge, 1},      // ⚖ research/11 §1.2.2 zombie table
        {GoalKind::TargetPlayer, 2}, // ⚖
        {GoalKind::MeleeAttack, 2},  // ⚖ 优先级 2 有两个目标 - the concurrency point
        {GoalKind::RandomStroll, 7}, // ⚖
        {GoalKind::LookAtPlayer, 8}, // ⚖
    };
    return def;
}

// The walking bomb - the exploder sample. research/01 §10.2 creeper row: HP 20,
// speed attr 0.25, explosion power 3 (normal ~49 / hard 64.5), XP 5.
// research/11 §1.5.5: player within 3 with line of sight lights a 30-tick fuse,
// and breaking line of sight OR reaching 7 blocks cancels it.
MobDef blastbud_def(const ItemRegistry &items) {
    (void) items; // no loot: the base game's exploder drops a material this
                  // project has no item for, and inventing one is content work
    MobDef def;
    // ⚠ 体形: the same sourcing gap as the wretch (no box in the sources for a
    // hostile mob). HP 20 is ⚖ docs/01 §6.
    def.physics = mob_physics("Blastbud", 0.3, 1.7, 20);

    def.mob_class = MobClass::Hostile;
    def.move_speed = 0.25; // ⚖ research/01 §10.2

    def.sight_range = 16.0; // ⚖ 多数生物 16 (research/11 §1.3.1)
    // ⚠ The exploder's own follow_range is not listed in the source; the
    // attribute default 32 applies (the "骷髅/蜘蛛/其他 16" row is for the
    // skeleton/spider line). This is the field the wiki conflicts about - see
    // mob_type.hpp.
    def.follow_range = 32.0;
    def.tempt_range = 0.0;

    def.stroll_trigger_range = 32.0;
    def.stroll_radius = 10.0; // ⚠ 待校准

    // ⚖ research/11 §1.5.5
    def.fuse_trigger_range = 3.0;
    def.fuse_cancel_range = 7.0;
    def.fuse_ticks = 30;
    def.explosion_power = 3.0;          // ⚖ docs/01 §6
    def.explosion_damage_easy = 49.0;   // ⚠ 无来源 (待校准; 取普通值)
    def.explosion_damage_normal = 49.0; // ⚖ research/01 §10.2
    def.explosion_damage_hard = 64.5;   // ⚖ research/01 §10.2
    def.explosion_radius = 6.0;         // ⚠ 待校准 (2 x power)

    // no melee damage: it does not hit, it detonates
    def.panic_ticks = 0;
    def.goals = {
        {GoalKind::Revenge, 1},                            // it still turns on whoever hurt it
        {GoalKind::TargetPlayer, 2}, {GoalKind::Swell, 3}, // 蓄爆 sits below the target selection
        {GoalKind::RandomStroll, 7}, {GoalKind::LookAtPlayer, 8},
    };
    return def;
}

} // namespace

double MobDef::damage_for(const Difficulty difficulty) const {
    switch (difficulty) {
    case Difficulty::Peaceful:
        return 0.0; // ⚖ 和平难度: hostile mobs do not exist; 0 is the safe answer
    case Difficulty::Easy:
        return damage_easy;
    case Difficulty::Hard:
        return damage_hard;
    case Difficulty::Normal:
        break;
    }
    return damage_normal;
}

double MobDef::explosion_damage_for(const Difficulty difficulty) const {
    switch (difficulty) {
    case Difficulty::Peaceful:
        return 0.0;
    case Difficulty::Easy:
        return explosion_damage_easy;
    case Difficulty::Hard:
        return explosion_damage_hard;
    case Difficulty::Normal:
        break;
    }
    return explosion_damage_normal;
}

double MobDef::explosion_falloff(const double distance) const {
    if (explosion_radius <= 0.0) {
        return 0.0;
    }
    const double ratio = 1.0 - distance / explosion_radius;
    return ratio > 0.0 ? ratio : 0.0;
}

std::uint16_t MobRegistry::register_mob(EntityTypeRegistry &types, std::string id, MobDef def) {
    if (id.empty()) {
        throw std::invalid_argument("mob id must not be empty");
    }
    if (index_by_id_.contains(id)) {
        throw std::invalid_argument("mob id already registered: " + id);
    }
    // One truth about "is this entity a mob": the EntityDef says so, and the
    // stepping passes read it. A MobDef registered against a def that still says
    // Item would be stepped by the DROP rules - fail at startup instead.
    if (def.physics.entity_class != EntityClass::Mob) {
        throw std::invalid_argument("mob def must be marked EntityClass::Mob: " + id);
    }
    // Content references are already numeric by the time they arrive: the table
    // resolves them with ItemRegistry::id_of, which throws on an unknown name.
    // Nothing to re-check here - a dropped item id that survived that call is
    // registered by construction.
    const std::uint16_t entity_type = types.register_type(id, def.physics);
    index_by_id_.emplace(id, defs_.size());
    index_by_entity_type_.emplace(entity_type, defs_.size());
    defs_.push_back(std::move(def));
    entity_type_of_.push_back(entity_type);
    return entity_type;
}

MobRegistry MobRegistry::create_default(EntityTypeRegistry &types, const ItemRegistry &items) {
    MobRegistry registry;
    registry.register_mob(types, "mossback", mossback_def(items));
    registry.register_mob(types, "hollow_wretch", hollow_wretch_def(items));
    registry.register_mob(types, "blastbud", blastbud_def(items));
    return registry;
}

const MobDef *MobRegistry::find(const std::uint16_t entity_type) const {
    const auto it = index_by_entity_type_.find(entity_type);
    if (it == index_by_entity_type_.end()) {
        return nullptr;
    }
    return &defs_[it->second];
}

const MobDef *MobRegistry::find_by_id(const std::string_view id) const {
    const auto it = index_by_id_.find(id);
    if (it == index_by_id_.end()) {
        return nullptr;
    }
    return &defs_[it->second];
}

std::uint16_t MobRegistry::entity_type_of(const std::string_view id) const {
    const auto it = index_by_id_.find(id);
    if (it == index_by_id_.end()) {
        throw std::out_of_range("unknown mob id: " + std::string(id));
    }
    return entity_type_of_[it->second];
}

std::vector<std::uint16_t> MobRegistry::entity_types(const MobClass mob_class) const {
    std::vector<std::uint16_t> out;
    for (std::size_t i = 0; i < defs_.size(); ++i) {
        if (defs_[i].mob_class == mob_class) {
            out.push_back(entity_type_of_[i]);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace opencraft::game
