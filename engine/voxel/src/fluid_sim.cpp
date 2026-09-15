#include "opencraft/voxel/fluid_sim.hpp"

#include <algorithm>
#include <array>

namespace opencraft::voxel {

namespace {

// Horizontal directions, index-parallel with the kFluid* bits. Order fixes
// the tie iteration order of §4.5 (all tied directions emit, so the order only
// affects BFS traversal, never the result).
constexpr int kDirOffsetX[4] = {1, -1, 0, 0}; // East, West, South, North
constexpr int kDirOffsetZ[4] = {0, 0, 1, -1};
constexpr std::uint8_t kDirBits[4] = {kFluidEast, kFluidWest, kFluidSouth, kFluidNorth};

// Bit a neighbour at offset d must carry to be emitting *toward* this cell:
// the neighbour east of P pushes west, and so on.
constexpr std::uint8_t kEmitBitToward[4] = {kFluidWest, kFluidEast, kFluidNorth, kFluidSouth};

// "no drop-off reachable" marker for the cost vector (§4.5 uses the same
// sentinel role as the wiki's initial flow weight of 1000).
constexpr int kSlopeCostInfinity = 1 << 20;

// The BFS stays inside the (2r+1)^2 horizontal box around the flowing cell;
// with the radius fixed by fluid_slope_radius() that is at most 11x11.
constexpr int kSlopeMaxRadius = 5;
constexpr int kSlopeMaxCells = (2 * kSlopeMaxRadius + 1) * (2 * kSlopeMaxRadius + 1);

// A "drop-off hole" is a cell whose own below can take fluid (§4.3).
[[nodiscard]] bool is_drop_off(const IFluidWorld &world, int world_x, int world_y, int world_z) {
    return world_y - 1 >= 0 && world.fluid_may_enter(world_x, world_y - 1, world_z);
}

} // namespace

FluidSim::FluidSim(IFluidWorld &world) : world_(&world) {
}

bool FluidSim::open_below(int world_x, int world_y, int world_z) const {
    const int below_y = world_y - 1;
    if (below_y < 0) {
        return false;
    }
    const FluidCell below = unpack_fluid(world_->fluid_at(world_x, below_y, world_z));
    if (below.kind != FluidKind::None && below.source && below.level >= kFluidMaxLevel) {
        return false; // §3.4 rule 3: the floor is already a same-kind source
    }
    return world_->fluid_may_enter(world_x, below_y, world_z);
}

int FluidSim::direction_cost(int world_x, int world_y, int world_z, int direction, int radius) const {
    const int step_x = kDirOffsetX[direction];
    const int step_z = kDirOffsetZ[direction];
    const int side = 2 * radius + 1;

    // §4.4 step 2: the direction has to be open at all.
    if (!world_->fluid_may_enter(world_x + step_x, world_y, world_z + step_z)) {
        return kSlopeCostInfinity;
    }
    // §4.4 step 3: the first cell is already a drop-off -> cost 1.
    if (is_drop_off(*world_, world_x + step_x, world_y, world_z + step_z)) {
        return 1;
    }

    // Unweighted BFS in the horizontal plane; the first drop-off found is the
    // nearest one, so no priority queue is needed (§4.4).
    std::array<bool, kSlopeMaxCells> visited{};

    struct Node {
        int dx;
        int dz;
        int cost;
    };

    std::array<Node, kSlopeMaxCells> queue{};
    const auto index_of = [radius, side](int dx, int dz) {
        return static_cast<std::size_t>(dx + radius) * static_cast<std::size_t>(side) +
               static_cast<std::size_t>(dz + radius);
    };

    visited[index_of(step_x, step_z)] = true;
    std::size_t head = 0;
    std::size_t tail = 0;
    queue[tail++] = Node{step_x, step_z, 1};
    while (head < tail) {
        const Node node = queue[head++];
        if (node.cost >= radius) {
            continue; // outside the search radius
        }
        for (int d = 0; d < 4; ++d) {
            const int next_dx = node.dx + kDirOffsetX[d];
            const int next_dz = node.dz + kDirOffsetZ[d];
            if (next_dx < -radius || next_dx > radius || next_dz < -radius || next_dz > radius) {
                continue;
            }
            const std::size_t index = index_of(next_dx, next_dz);
            if (visited[index]) {
                continue;
            }
            if (!world_->fluid_may_enter(world_x + next_dx, world_y, world_z + next_dz)) {
                continue; // solid faces block the search too
            }
            visited[index] = true;
            if (is_drop_off(*world_, world_x + next_dx, world_y, world_z + next_dz)) {
                return node.cost + 1;
            }
            queue[tail++] = Node{next_dx, next_dz, node.cost + 1};
        }
    }
    return kSlopeCostInfinity;
}

std::uint8_t FluidSim::slope_spread(int world_x, int world_y, int world_z, FluidKind kind) const {
    const int radius = fluid_slope_radius(kind);
    int costs[4] = {};
    int best = kSlopeCostInfinity;
    for (int d = 0; d < 4; ++d) {
        costs[d] = direction_cost(world_x, world_y, world_z, d, radius);
        best = std::min(best, costs[d]);
    }

    std::uint8_t mask = 0;
    if (best >= kSlopeCostInfinity) {
        // §4.5 fallback: no drop-off inside the radius, so the fluid spreads
        // symmetrically over every direction it can actually enter.
        for (int d = 0; d < 4; ++d) {
            if (world_->fluid_may_enter(world_x + kDirOffsetX[d], world_y, world_z + kDirOffsetZ[d])) {
                mask |= kDirBits[d];
            }
        }
        return mask;
    }
    // Only the cheapest direction(s) flow; every tied direction flows.
    for (int d = 0; d < 4; ++d) {
        if (costs[d] == best) {
            mask |= kDirBits[d];
        }
    }
    return mask;
}

FluidCell FluidSim::compute(int world_x, int world_y, int world_z, FluidCell current) const {
    // A cell that can no longer hold fluid (a block was placed into it) drops
    // whatever it had.
    if (!world_->fluid_may_enter(world_x, world_y, world_z)) {
        return FluidCell{};
    }

    const FluidCell above = unpack_fluid(world_->fluid_at(world_x, world_y + 1, world_z));
    const bool fed_from_above = above.kind != FluidKind::None && above.level > 0;
    const bool open = open_below(world_x, world_y, world_z);

    // §3.3: a cell under the same fluid is full. While its floor is open it is
    // a falling column and emits nothing horizontally, which is what keeps a
    // waterfall one block wide; once it lands it behaves like a source of
    // level 8 - decay to its neighbours.
    if (fed_from_above) {
        FluidCell next;
        next.kind = above.kind;
        next.level = kFluidMaxLevel;
        next.source = current.source && current.kind == above.kind;
        next.falling = open;
        next.spread = open ? std::uint8_t{0} : slope_spread(world_x, world_y, world_z, next.kind);
        return next;
    }

    // A bucket-placed source keeps its level; it is not derived from
    // neighbours (source regeneration is a later card).
    if (current.source && current.kind != FluidKind::None && current.level > 0) {
        FluidCell next;
        next.kind = current.kind;
        next.level = kFluidMaxLevel;
        next.source = true; // a re-evaluated source stays a source
        next.falling = open;
        next.spread = open ? std::uint8_t{0} : slope_spread(world_x, world_y, world_z, next.kind);
        return next;
    }

    // §3.2: inherit from the strongest neighbour of N4*, i.e. one that can
    // effectively flow toward this cell. A falling column carries an empty
    // emit set, so it never feeds a neighbour sideways.
    int best = 0;
    FluidKind best_kind = FluidKind::None;
    for (int d = 0; d < 4; ++d) {
        const FluidCell neighbour =
            unpack_fluid(world_->fluid_at(world_x + kDirOffsetX[d], world_y, world_z + kDirOffsetZ[d]));
        if (neighbour.kind == FluidKind::None || neighbour.level <= 0) {
            continue;
        }
        if ((neighbour.spread & kEmitBitToward[d]) == 0) {
            continue;
        }
        const int candidate = static_cast<int>(neighbour.level) - fluid_level_decay(neighbour.kind);
        if (candidate > best) {
            best = candidate;
            best_kind = neighbour.kind;
        }
    }
    if (best <= 0) {
        return FluidCell{}; // level exhausted: the flow stops here (§3.2)
    }

    FluidCell next;
    next.kind = best_kind;
    next.level = static_cast<std::uint8_t>(std::min(best, kFluidMaxLevel));
    next.falling = false;
    next.spread = open ? std::uint8_t{0} : slope_spread(world_x, world_y, world_z, best_kind);
    return next;
}

void FluidSim::schedule(int world_x, int world_y, int world_z, std::uint64_t due) {
    const std::int64_t key = fluid_pos_key(world_x, world_y, world_z);
    if (scheduled_.contains(key)) {
        return; // already queued: one pending entry per position
    }
    if (scheduled_.size() >= kMaxPending) {
        ++dropped_;
        return;
    }
    scheduled_.insert(key);
    queue_[due].push_back(Cell{world_x, world_y, world_z});
}

void FluidSim::update(const Cell &cell, std::uint64_t due) {
    ++processed_;
    const FluidCell current = unpack_fluid(world_->fluid_at(cell.x, cell.y, cell.z));
    const FluidCell next = compute(cell.x, cell.y, cell.z, current);
    if (next == current) {
        return; // converged here: no neighbour needs to hear about it
    }
    world_->set_fluid_at(cell.x, cell.y, cell.z, pack_fluid(next));

    // Neighbours re-evaluate one interval out, measured from this cell's own
    // due tick so the chain advances exactly one block per interval.
    const FluidKind kind = next.kind != FluidKind::None ? next.kind : current.kind;
    const std::uint64_t next_due = due + fluid_tick_interval(kind);
    schedule(cell.x + 1, cell.y, cell.z, next_due);
    schedule(cell.x - 1, cell.y, cell.z, next_due);
    schedule(cell.x, cell.y + 1, cell.z, next_due);
    schedule(cell.x, cell.y - 1, cell.z, next_due);
    schedule(cell.x, cell.y, cell.z + 1, next_due);
    schedule(cell.x, cell.y, cell.z - 1, next_due);
}

void FluidSim::step() {
    ++tick_;
    while (!queue_.empty()) {
        auto first = queue_.begin();
        if (first->first > tick_) {
            break;
        }
        // Move the batch out first: update() may insert new due keys.
        std::vector<Cell> batch = std::move(first->second);
        const std::uint64_t due = first->first;
        queue_.erase(first);
        for (const Cell &cell : batch) {
            scheduled_.erase(fluid_pos_key(cell.x, cell.y, cell.z));
            update(cell, due);
        }
    }
}

void FluidSim::place_source(int world_x, int world_y, int world_z, FluidKind kind) {
    FluidCell cell;
    cell.kind = kind;
    cell.level = kFluidMaxLevel;
    cell.source = true;
    // A source standing over an opening flows straight down instead of
    // spreading sideways (§3.4), so it needs the same gate compute() applies.
    const bool open = open_below(world_x, world_y, world_z);
    cell.falling = open;
    cell.spread = open ? std::uint8_t{0} : slope_spread(world_x, world_y, world_z, kind);
    world_->set_fluid_at(world_x, world_y, world_z, pack_fluid(cell));

    const std::uint64_t due = tick_ + fluid_tick_interval(kind);
    schedule(world_x + 1, world_y, world_z, due);
    schedule(world_x - 1, world_y, world_z, due);
    schedule(world_x, world_y + 1, world_z, due);
    schedule(world_x, world_y - 1, world_z, due);
    schedule(world_x, world_y, world_z + 1, due);
    schedule(world_x, world_y, world_z - 1, due);
}

void FluidSim::clear_cell(int world_x, int world_y, int world_z) {
    const FluidCell current = unpack_fluid(world_->fluid_at(world_x, world_y, world_z));
    if (current.empty()) {
        return;
    }
    world_->set_fluid_at(world_x, world_y, world_z, 0);
    const std::uint64_t due = tick_ + fluid_tick_interval(current.kind);
    schedule(world_x + 1, world_y, world_z, due);
    schedule(world_x - 1, world_y, world_z, due);
    schedule(world_x, world_y + 1, world_z, due);
    schedule(world_x, world_y - 1, world_z, due);
    schedule(world_x, world_y, world_z + 1, due);
    schedule(world_x, world_y, world_z - 1, due);
}

void FluidSim::on_block_changed(int world_x, int world_y, int world_z) {
    wake(world_x, world_y, world_z);
}

void FluidSim::wake(int world_x, int world_y, int world_z) {
    const FluidCell here = unpack_fluid(world_->fluid_at(world_x, world_y, world_z));
    const std::uint64_t due = tick_ + fluid_tick_interval(here.kind);
    schedule(world_x, world_y, world_z, due);
    schedule(world_x + 1, world_y, world_z, due);
    schedule(world_x - 1, world_y, world_z, due);
    schedule(world_x, world_y + 1, world_z, due);
    schedule(world_x, world_y - 1, world_z, due);
    schedule(world_x, world_y, world_z + 1, due);
    schedule(world_x, world_y, world_z - 1, due);
}

} // namespace opencraft::voxel
