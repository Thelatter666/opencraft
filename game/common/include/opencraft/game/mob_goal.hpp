#pragma once

// T-M2: the goal stack (目标栈) - the AI structure the M2c launch roster uses.
//
// research/11 §1.1 settled the architecture question: the base game runs TWO AI
// systems side by side (the old goal stack and the newer brain), and the ENTIRE
// M2c launch roster - passive, neutral and hostile alike - sits on the goal
// stack side. So the brain is deliberately absent here; it is M4+ work for the
// complex mobs (research/11 R-8).
//
// ── WHAT THIS HEADER IS, AND WHY IT IS PURE ─────────────────────────────────
// The scheduler below is a pure function of (running mask, goal table, two
// verdict arrays). It knows nothing about the world, the entity store or
// physics, which is what makes ★ acceptance item 1 - "低优先级目标可与高优先级
// 同时运行" - a property that can be asserted directly instead of inferred from
// a mob's observed behaviour. The goal IMPLEMENTATIONS (what "can I attack"
// means) live in server/sim/mob_sim.hpp; only the SCHEDULING RULE lives here,
// because the rule is the thing the launch roster shares.
//
// ── THE ONE RULE THAT MUST NOT BE BROKEN (research/11 §1.2.2) ───────────────
// A goal stack must NOT be implemented as "run the first goal whose canUse is
// true". The wiki's own zombie table puts two goals at priority 2 and two more
// at priority 8; low-priority goals keep running while a high-priority one runs
// ("看" and "游荡" run alongside the chase). If it is implemented as
// first-match-wins, a zombie only ever retaliates: it never strolls and never
// looks around, and the behaviour is visibly wrong.
//
// So `scan_goals` visits the WHOLE table and:
//   * starts every goal whose can_use is true and which is not running;
//   * stops every running goal whose continue_use is false.
// Nothing else. Priority decides the ORDER callbacks are applied (a higher
// priority goal writes the shared state first, so a lower one reading it this
// tick sees the decision), never WHETHER a goal runs. Mutual exclusion is
// expressed by a goal's own can_use - e.g. the stroll goal's can_use reads
// "do I already have a target?", exactly as the source describes.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace opencraft::game {

// The goal library of research/11 §1.2.3, cut down to the goals the three
// launch mobs actually need (the table also lists bow attack, follow parent,
// float, eat grass, door interact, flee sun and avoid-entity; those are
// follow-up content, not this card's - see the report's "what a new mob needs"
// section for how one is added).
//
// The enum is an IDENTITY, not a priority: the same goal sits at different
// priorities on different mobs (TargetPlayer is priority 2 on the hostile
// roster and absent from the passive one), so the priority is content and lives
// in MobDef.
enum class GoalKind : std::uint8_t {
    Panic = 0,    // 恐慌: flee for a few seconds after taking damage (passive)
    Revenge,      // 反击: go after whatever damaged me (universal)
    TargetPlayer, // 主动选目标 - 玩家: lock onto the player (hostile)
    MeleeAttack,  // 近战攻击: close to melee reach and hit the target
    Swell,        // 蓄爆: the exploder's fuse
    Tempt,        // 引诱: follow a player holding the breeding food (passive)
    Breed,        // 求偶: walk to the partner, then produce a baby
    RandomStroll, // 随机游荡
    LookAtPlayer, // 环视 / 注视实体
    Count,
};

inline constexpr std::size_t kGoalKindCount = static_cast<std::size_t>(GoalKind::Count);

// One bit per GoalKind, which is the whole per-mob goal state: what a mob has
// running. A bitmask rather than a container because it is state that crosses a
// wire in M3 (the authority owns the mob) and because "both goals are running"
// has to be one comparable value for the tests.
using GoalMask = std::uint32_t;

[[nodiscard]] constexpr GoalMask goal_bit(const GoalKind kind) {
    return static_cast<GoalMask>(1u) << static_cast<unsigned>(kind);
}

[[nodiscard]] constexpr bool goal_running(const GoalMask running, const GoalKind kind) {
    return (running & goal_bit(kind)) != 0;
}

// One entry of a mob's goal table: which goal, and at which priority.
// 1 = highest (research/11 §1.2.2: 优先级数字越小越高).
struct GoalEntry {
    GoalKind kind = GoalKind::RandomStroll;
    std::uint8_t priority = 1;
};

// A mob's goal table, sorted by (priority ascending, kind ascending) - the scan
// order. The kind tiebreak is what makes the order TOTAL and therefore
// reproducible: the wiki's tables really do hold several goals at one priority,
// and without a tiebreak the callback order would depend on the order rows
// happened to be written in.
[[nodiscard]] std::vector<GoalEntry> sorted_goals(std::vector<GoalEntry> goals);

// The priority a mob assigns to a goal, or 0 when the mob does not have it.
[[nodiscard]] std::uint8_t goal_priority(const std::vector<GoalEntry> &goals, GoalKind kind);

// One tick's verdicts, filled in by the goal implementations in mob_sim.hpp:
// per goal, whether it may start now and whether it may keep running.
//
// Two separate arrays on purpose - the source keeps canUse and continueUse
// apart, and the difference is visible in the game: the tempt goal may not
// START beyond 6 blocks (research/11 §1.5.6: 实际开始跟随半径 6 格) but keeps
// running out to 10 (牛 stops at ≥10, §6.3).
struct GoalVerdicts {
    std::array<bool, kGoalKindCount> can_use{};
    std::array<bool, kGoalKindCount> continue_use{};
};

// What one scan did, per goal. `started`/`stopped` are the callbacks the caller
// then applies (the scheduler does not touch the world).
struct GoalScanResult {
    GoalMask running = 0;
    std::array<bool, kGoalKindCount> started{};
    std::array<bool, kGoalKindCount> stopped{};
};

// ★ The scan. See the header comment for the rule; the short version is
// "visit everything, start what can start, stop what cannot continue".
//
// `order` is the mob's sorted goal table. Note what is NOT here: no
// "highest available priority wins", no early exit, no exclusive slot.
[[nodiscard]] inline GoalScanResult scan_goals(const GoalMask running, const std::vector<GoalEntry> &order,
                                               const GoalVerdicts &verdicts) {
    GoalScanResult result;
    result.running = running;
    for (const GoalEntry &entry : order) {
        const std::size_t index = static_cast<std::size_t>(entry.kind);
        const bool was_running = (result.running & goal_bit(entry.kind)) != 0;
        if (was_running && !verdicts.continue_use[index]) {
            result.running &= ~goal_bit(entry.kind);
            result.stopped[index] = true;
        }
        // The can_use check is unconditional: a goal is never skipped just
        // because a higher-priority goal is running. Read the mask bit again
        // rather than trusting `was_running`, so a goal stopped in this same
        // scan can also restart in it (the two verdicts are independent).
        if (!(result.running & goal_bit(entry.kind)) && verdicts.can_use[index]) {
            result.running |= goal_bit(entry.kind);
            result.started[index] = true;
        }
    }
    return result;
}

} // namespace opencraft::game
