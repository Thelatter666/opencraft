#pragma once

#include <cstdint>
#include <map>
#include <unordered_set>
#include <vector>

#include "opencraft/voxel/fluid.hpp"

namespace opencraft::voxel {

// World access the fluid simulation needs, injected so FluidSim stays unit
// testable without a ChunkManager (the pattern ILightWorld established for the
// light engine in T006).
class IFluidWorld {
public:
    virtual ~IFluidWorld() = default;

    // Packed fluid cell at a world position, 0 when there is none. Must answer
    // without side effects for unloaded chunks and any y, including negative.
    [[nodiscard]] virtual std::uint16_t fluid_at(int world_x, int world_y, int world_z) const = 0;

    // Writes a packed fluid cell (0 clears). Implementations keep the block
    // layer in sync: a non-empty cell means the fluid placeholder block, an
    // empty one restores air.
    virtual void set_fluid_at(int world_x, int world_y, int world_z, std::uint16_t cell) = 0;

    // True when a fluid may occupy this position: air, or a replaceable
    // (liquid) block. Unloaded chunks and out-of-world y answer false, so the
    // simulation treats them as walls (docs/research/10 §4.7, R-5 - the
    // opposite of ILightWorld's "unloaded reads as air").
    [[nodiscard]] virtual bool fluid_may_enter(int world_x, int world_y, int world_z) const = 0;
};

// Scheduled-tick fluid simulation (docs/research/10 §2 / §3 / §4).
//
// The queue holds (world position, due tick) entries in due order; a position
// is queued at most once at a time (MC's per-position scheduled-tick dedup), so
// repeated neighbour updates collapse instead of piling up. step() advances one
// game tick and drains everything due at it. A cell whose state changed
// re-schedules its six neighbours one interval later, which is what makes water
// advance one block per 5 ticks on flat ground.
//
// All state is per-voxel and the update rule is a pure function of the world,
// so the result does not depend on chunk loading order. Single-threaded;
// callers serialize access, like LightEngine.
class FluidSim {
public:
    // §8.4 / R-5: JE caps scheduled ticks at 65,536 per tick and the wiki does
    // not say what happens past the cap. OpenCraft rejects the new entry and
    // counts it, matching TickClock's dropped_ticks philosophy.
    static constexpr std::size_t kMaxPending = 65536;

    explicit FluidSim(IFluidWorld &world);

    // Places a source of `kind` (the bucket path) and wakes the neighbours so
    // the first spread step happens one interval later.
    void place_source(int world_x, int world_y, int world_z, FluidKind kind = FluidKind::Water);

    // Removes any fluid at the position (bucket pickup) and wakes the
    // neighbours so the surrounding water recedes.
    void clear_cell(int world_x, int world_y, int world_z);

    // Block-update hook (docs/research/10 §7.2): the world data changed here,
    // so this cell and its six neighbours re-evaluate on the fluid's own
    // schedule.
    void on_block_changed(int world_x, int world_y, int world_z);

    // Schedules this cell and its six neighbours one interval out. Used by
    // on_block_changed and by the chunk-load border scan.
    void wake(int world_x, int world_y, int world_z);

    // Advances one game tick and processes every entry due at it.
    void step();

    [[nodiscard]] std::uint64_t tick() const { return tick_; }

    [[nodiscard]] std::size_t pending() const { return scheduled_.size(); }

    [[nodiscard]] std::uint64_t dropped() const { return dropped_; }

    // Cells examined by update(); diagnostics for the back-pressure report.
    [[nodiscard]] std::uint64_t processed() const { return processed_; }

private:
    struct Cell {
        int x = 0;
        int y = 0;
        int z = 0;
    };

    void schedule(int world_x, int world_y, int world_z, std::uint64_t due);
    void update(const Cell &cell, std::uint64_t due);

    // The transfer rule of docs/research/10 §3: vertical first (§3.4),
    // otherwise the strongest horizontal contributor of N4* (§3.2).
    [[nodiscard]] FluidCell compute(int world_x, int world_y, int world_z, FluidCell current) const;

    // Emit set of a cell: the direction(s) it pushes fluid toward
    // (docs/research/10 §4.4 / §4.5).
    [[nodiscard]] std::uint8_t slope_spread(int world_x, int world_y, int world_z, FluidKind kind) const;

    // Steps along one direction to the nearest drop-off, INF when the radius
    // holds none (§4.4).
    [[nodiscard]] int direction_cost(int world_x, int world_y, int world_z, int direction, int radius) const;

    // True when the cell below can take fluid and is not already a same-kind
    // source (§3.4 rule 3).
    [[nodiscard]] bool open_below(int world_x, int world_y, int world_z) const;

    IFluidWorld *world_;
    std::map<std::uint64_t, std::vector<Cell>> queue_;
    std::unordered_set<std::int64_t> scheduled_;
    std::uint64_t tick_ = 0;
    std::uint64_t dropped_ = 0;
    std::uint64_t processed_ = 0;
};

// Packs a world position into the i64 key the simulation's bookkeeping uses:
// 21 bits per axis (x/z to ±1M blocks, y to 0..2M).
[[nodiscard]] constexpr std::int64_t fluid_pos_key(int x, int y, int z) {
    return (static_cast<std::int64_t>(x & 0x1FFFFF) << 42) | (static_cast<std::int64_t>(z & 0x1FFFFF) << 21) |
           static_cast<std::int64_t>(y & 0x1FFFFF);
}

} // namespace opencraft::voxel
