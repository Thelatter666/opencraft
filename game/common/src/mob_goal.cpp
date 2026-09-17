#include "opencraft/game/mob_goal.hpp"

#include <algorithm>

namespace opencraft::game {

// Sort by priority ascending, then by GoalKind: the scan order. Priority alone
// is not a total order - the source's own zombie table holds two goals at
// priority 2 and two at 8 (research/11 §1.2.2) - and a non-total order would
// make the callback order depend on the order the table rows happened to be
// written in, which is exactly the kind of accidental coupling that makes a
// behaviour change impossible to review.
std::vector<GoalEntry> sorted_goals(std::vector<GoalEntry> goals) {
    std::sort(goals.begin(), goals.end(), [](const GoalEntry &lhs, const GoalEntry &rhs) {
        if (lhs.priority != rhs.priority) {
            return lhs.priority < rhs.priority;
        }
        return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
    });
    return goals;
}

std::uint8_t goal_priority(const std::vector<GoalEntry> &goals, const GoalKind kind) {
    for (const GoalEntry &entry : goals) {
        if (entry.kind == kind) {
            return entry.priority;
        }
    }
    return 0; // 0 = the mob does not have this goal at all
}

} // namespace opencraft::game
