#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <doctest/doctest.h>

#include "opencraft/render/mesher.hpp"
#include "opencraft/voxel/block_registry.hpp"
#include "opencraft/voxel/chunk.hpp"
#include "opencraft/voxel/fluid_sim.hpp"

using namespace opencraft;
using voxel::FluidCell;
using voxel::FluidKind;
using voxel::FluidSim;

namespace {

// In-memory IFluidWorld for the simulation: everything below `floor_y` is
// solid ground unless carved, optional explicit wall columns, and a loaded
// region that is deliberately finite so the out-of-region behaviour (walls,
// docs/research/10 §4.7) is exercised too.
class TestFluidWorld final : public voxel::IFluidWorld {
public:
    static constexpr int kLoadedRadius = 24;
    static constexpr int kLoadedHeight = 48;

    explicit TestFluidWorld(int floor_y) : floor_y_(floor_y) {}

    [[nodiscard]] std::uint16_t fluid_at(int x, int y, int z) const override {
        const auto it = fluid_.find(key(x, y, z));
        return it == fluid_.end() ? 0 : it->second;
    }

    void set_fluid_at(int x, int y, int z, std::uint16_t cell) override {
        if (cell == 0) {
            fluid_.erase(key(x, y, z));
        } else {
            fluid_[key(x, y, z)] = cell;
        }
    }

    [[nodiscard]] bool fluid_may_enter(int x, int y, int z) const override {
        if (!loaded(x, y, z)) {
            return false; // unloaded = wall
        }
        if (y < floor_y_ && !carved_.contains(key(x, y, z))) {
            return false; // solid ground, unless carved out
        }
        return !walls_.contains(key(x, y, z));
    }

    [[nodiscard]] bool loaded(int x, int y, int z) const {
        return x >= -kLoadedRadius && x <= kLoadedRadius && z >= -kLoadedRadius && z <= kLoadedRadius && y >= 0 &&
               y < kLoadedHeight;
    }

    // Removes ground so a cell below the floor line becomes air (a shaft or a
    // drop-off hole).
    void carve(int x, int y, int z) { carved_.insert(key(x, y, z)); }

    // Adds a solid obstacle above the floor line.
    void wall(int x, int y, int z) { walls_.insert(key(x, y, z)); }

    [[nodiscard]] FluidCell cell_at(int x, int y, int z) const { return voxel::unpack_fluid(fluid_at(x, y, z)); }

    [[nodiscard]] int level_at(int x, int y, int z) const { return cell_at(x, y, z).level; }

    [[nodiscard]] int floor_y() const { return floor_y_; }

    [[nodiscard]] std::size_t water_cells() const {
        std::size_t count = 0;
        for (const auto &[packed, cell] : fluid_) {
            static_cast<void>(packed);
            if (!voxel::unpack_fluid(cell).empty()) {
                ++count;
            }
        }
        return count;
    }

private:
    static std::int64_t key(int x, int y, int z) { return voxel::fluid_pos_key(x, y, z); }

    int floor_y_ = 0;
    std::unordered_set<std::int64_t> carved_;
    std::unordered_set<std::int64_t> walls_;
    std::unordered_map<std::int64_t, std::uint16_t> fluid_;
};

void run(FluidSim &sim, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        sim.step();
    }
}

} // namespace

// ── data layer ──────────────────────────────────────────────────────────────

TEST_CASE("fluid: packed cells round trip and reject corrupt payloads") {
    FluidCell cell;
    cell.kind = FluidKind::Water;
    cell.level = 7;
    cell.falling = true;
    cell.source = true;
    cell.spread = voxel::kFluidWest | voxel::kFluidNorth;
    CHECK(voxel::unpack_fluid(voxel::pack_fluid(cell)) == cell);
    CHECK(voxel::valid_packed_fluid(voxel::pack_fluid(cell)));

    CHECK(voxel::pack_fluid(FluidCell{}) == 0);
    CHECK(voxel::unpack_fluid(0).empty());

    // Level 9 does not exist (§0.3 caps the level at 8).
    CHECK_FALSE(voxel::valid_packed_fluid(static_cast<std::uint16_t>((1U << 14) | (9U << 10))));
    // The reserved kind is never written.
    CHECK_FALSE(voxel::valid_packed_fluid(static_cast<std::uint16_t>((3U << 14) | (1U << 10))));
    // The spare nibble must be zero.
    CHECK_FALSE(voxel::valid_packed_fluid(static_cast<std::uint16_t>((1U << 14) | (3U << 10) | 0x10U)));
}

TEST_CASE("fluid: chunk fluid sections store, clear and report emptiness") {
    voxel::Chunk chunk;
    CHECK(chunk.fluid_section_empty(0));
    CHECK_FALSE(chunk.has_fluid());

    FluidCell cell;
    cell.kind = FluidKind::Water;
    cell.level = 3;
    chunk.set_fluid(2, 33, 4, cell); // section 2
    CHECK_FALSE(chunk.fluid_section_empty(2));
    CHECK(chunk.fluid_section_empty(0));
    CHECK(chunk.has_fluid());
    CHECK(chunk.get_fluid(2, 33, 4) == cell);

    // Neighbouring cells stay empty; clearing the only cell empties its data
    // but the section keeps its storage (the no-shrink policy PaletteSection
    // already documents), so has_fluid() - which answers "is any section
    // allocated" - stays true while fluid_section_empty() does not.
    CHECK(chunk.get_fluid(3, 33, 4).empty());
    chunk.set_fluid(2, 33, 4, FluidCell{});
    CHECK(chunk.get_fluid(2, 33, 4).empty());
    CHECK_FALSE(chunk.fluid_section_empty(2));

    // Out-of-chunk probes answer empty instead of throwing (the simulation
    // probes across borders through its world interface, not through Chunk).
    CHECK(chunk.get_fluid(-1, 33, 4).empty());
    CHECK(chunk.get_fluid(2, voxel::Chunk::kSizeY, 4).empty());
}

TEST_CASE("fluid: serialization round trips the fluid layer") {
    voxel::Chunk chunk;
    chunk.set_block(0, 40, 0, 3);
    FluidCell source;
    source.kind = FluidKind::Water;
    source.level = 8;
    source.source = true;
    source.spread = voxel::kFluidAllDirections;
    chunk.set_fluid(0, 40, 0, source);
    FluidCell flow;
    flow.kind = FluidKind::Water;
    flow.level = 5;
    flow.spread = voxel::kFluidEast;
    chunk.set_fluid(15, 200, 15, flow); // section 12, a different section

    core::ByteBuffer buffer;
    chunk.serialize(buffer);
    buffer.rewind();
    const voxel::Chunk restored = voxel::Chunk::deserialize(buffer);
    CHECK(buffer.remaining() == 0);
    CHECK(restored.get_fluid(0, 40, 0) == source);
    CHECK(restored.get_fluid(15, 200, 15) == flow);
    CHECK(restored.get_block(0, 40, 0) == 3);
    CHECK(restored.has_fluid());
    CHECK(voxel::Chunk::kFormatVersion == 2);
}

TEST_CASE("fluid: a v1 payload still loads, with an empty fluid layer") {
    // Hand-built v1 payload (the wire format documented on Chunk): one
    // non-empty section, uniform value 7, i.e. bits = 0.
    core::ByteBuffer buffer;
    buffer.write_version(1);
    buffer.write_u8(1);  // non-empty section count
    buffer.write_u8(0);  // section index
    buffer.write_u8(0);  // bits per entry: 0 = uniform
    buffer.write_u16(1); // palette size
    buffer.write_u16(7); // the uniform block id
    buffer.rewind();

    const voxel::Chunk chunk = voxel::Chunk::deserialize(buffer);
    CHECK(buffer.remaining() == 0);
    CHECK(chunk.get_block(0, 0, 0) == 7);
    CHECK(chunk.get_block(15, 15, 15) == 7); // section 0 covers y in [0, 16)
    CHECK_FALSE(chunk.has_fluid());
    CHECK(chunk.fluid_section_empty(0));

    // A payload with a version nobody wrote is rejected instead of misread.
    core::ByteBuffer future;
    future.write_version(3);
    future.write_u8(0);
    future.rewind();
    CHECK_THROWS_AS([&] { static_cast<void>(voxel::Chunk::deserialize(future)); }(), std::runtime_error);
}

// ── transfer rules (acceptance 1-3) ─────────────────────────────────────────

TEST_CASE("fluid: a flat single source spreads one block per five ticks, at most seven") {
    TestFluidWorld world(8);
    FluidSim sim(world);
    sim.place_source(0, 8, 0);

    CHECK(world.cell_at(0, 8, 0).source);
    CHECK(world.cell_at(0, 8, 0).level == 8);

    run(sim, 5); // t = 5: the first ring is covered
    CHECK(world.level_at(1, 8, 0) == 7);
    CHECK(world.level_at(0, 8, 1) == 7);
    CHECK(world.level_at(-1, 8, 0) == 7);
    CHECK(world.level_at(0, 8, -1) == 7);
    CHECK(world.level_at(2, 8, 0) == 0);
    CHECK(world.level_at(1, 8, 1) == 0); // the diagonal follows one step later

    run(sim, 5); // t = 10: the second ring
    CHECK(world.level_at(2, 8, 0) == 6);
    CHECK(world.level_at(1, 8, 1) == 6);
    CHECK(world.level_at(3, 8, 0) == 0);

    run(sim, 25); // t = 35: the outermost level-1 ring
    CHECK(world.level_at(7, 8, 0) == 1);
    CHECK(world.level_at(0, 8, 7) == 1);
    CHECK(world.level_at(8, 8, 0) == 0); // the level budget is exhausted
    CHECK(world.level_at(7, 8, 1) == 0); // Manhattan distance 8 stays dry

    run(sim, 5); // t = 40: nothing beyond seven blocks, ever
    CHECK(world.level_at(7, 8, 0) == 1);
    CHECK(world.level_at(8, 8, 0) == 0);
    CHECK(sim.pending() == 0); // the field has settled
}

TEST_CASE("fluid: the level drops by one per flowing block") {
    TestFluidWorld world(8);
    FluidSim sim(world);
    sim.place_source(0, 8, 0);
    run(sim, 35);

    for (int distance = 1; distance <= 7; ++distance) {
        CAPTURE(distance);
        CHECK(world.level_at(distance, 8, 0) == 8 - distance);
    }
}

TEST_CASE("fluid: an open floor wins over spreading sideways") {
    TestFluidWorld world(8);
    // A shaft straight down from the cell the source will occupy.
    for (int y = 3; y < 8; ++y) {
        world.carve(0, y, 0);
    }
    FluidSim sim(world);
    sim.place_source(0, 8, 0);

    run(sim, 5);
    // The cell below became a full falling column...
    const FluidCell below = world.cell_at(0, 7, 0);
    CHECK(below.kind == FluidKind::Water);
    CHECK(below.level == 8);
    CHECK(below.falling);
    // ...and the source did not spread sideways: a waterfall is one block wide.
    CHECK(world.level_at(1, 8, 0) == 0);
    CHECK(world.level_at(-1, 8, 0) == 0);
    CHECK(world.level_at(0, 8, 1) == 0);
    CHECK(world.cell_at(0, 7, 0).spread == 0); // a falling cell never emits

    run(sim, 5);
    CHECK(world.cell_at(0, 6, 0).falling);
    CHECK(world.level_at(1, 8, 0) == 0); // still one block wide
    run(sim, 25);
    CHECK(world.cell_at(0, 4, 0).falling);       // the column is still descending
    CHECK(world.level_at(0, 3, 0) == 8);         // and reaches the shaft floor at y = 3
    CHECK_FALSE(world.cell_at(0, 3, 0).falling); // landed: no longer a falling cell
}

// ── slope search (acceptance 4) ─────────────────────────────────────────────

TEST_CASE("fluid: water funnels toward a drop-off instead of fanning out") {
    TestFluidWorld world(8);
    // A drop-off four blocks due west of the source: only the west direction
    // can reach it within the radius, the others need six steps.
    for (int y = 4; y < 8; ++y) {
        world.carve(-4, y, 0);
    }
    FluidSim sim(world);
    sim.place_source(0, 8, 0);

    CHECK(world.cell_at(0, 8, 0).spread == voxel::kFluidWest);

    run(sim, 15);
    CHECK(world.level_at(-1, 8, 0) == 7);
    CHECK(world.level_at(-2, 8, 0) == 6);
    CHECK(world.level_at(-3, 8, 0) == 5);
    // The three non-preferred directions are suppressed, not merely slower.
    CHECK(world.level_at(1, 8, 0) == 0);
    CHECK(world.level_at(0, 8, 1) == 0);
    CHECK(world.level_at(0, 8, -1) == 0);

    run(sim, 10);
    // The flow arrives at the drop-off and goes down as a full column.
    const FluidCell falling = world.cell_at(-4, 7, 0);
    CHECK(falling.kind == FluidKind::Water);
    CHECK(falling.level == 8);
    CHECK(falling.falling);
}

TEST_CASE("fluid: with no drop-off in range the spread is symmetric") {
    TestFluidWorld world(8);
    FluidSim sim(world);
    sim.place_source(0, 8, 0);
    CHECK(world.cell_at(0, 8, 0).spread == voxel::kFluidAllDirections);
}

TEST_CASE("fluid: a drop-off just outside the search radius does not steer the flow") {
    // kSlopeSearchRadiusWater is 5, so a hole seven steps away is invisible to
    // the search and the spread stays symmetric. This pins the constant: it is
    // the value docs/research/10 §4.2 takes and T-R1 ruling 2 flags as
    // "待实机校准" (debt T-D22).
    CHECK(voxel::kSlopeSearchRadiusWater == 5);
    TestFluidWorld world(8);
    for (int y = 4; y < 8; ++y) {
        world.carve(-7, y, 0); // seven steps away: out of range
    }
    FluidSim sim(world);
    sim.place_source(0, 8, 0);
    CHECK(world.cell_at(0, 8, 0).spread == voxel::kFluidAllDirections);
}

TEST_CASE("fluid: the radius boundary itself still steers the flow") {
    // Five steps away is exactly the radius, so the hole is found and the
    // other three directions are suppressed.
    TestFluidWorld world(8);
    for (int y = 4; y < 8; ++y) {
        world.carve(-5, y, 0);
    }
    FluidSim sim(world);
    sim.place_source(0, 8, 0);
    CHECK(world.cell_at(0, 8, 0).spread == voxel::kFluidWest);
}

TEST_CASE("fluid: tied drop-off distances all flow") {
    TestFluidWorld world(8);
    // Two drop-offs, both four steps away: north and east.
    for (int y = 4; y < 8; ++y) {
        world.carve(0, y, -4);
        world.carve(4, y, 0);
    }
    FluidSim sim(world);
    sim.place_source(0, 8, 0);

    const std::uint8_t spread = world.cell_at(0, 8, 0).spread;
    CHECK((spread & voxel::kFluidNorth) != 0);
    CHECK((spread & voxel::kFluidEast) != 0);
    CHECK((spread & voxel::kFluidWest) == 0);
    CHECK((spread & voxel::kFluidSouth) == 0);

    run(sim, 30);
    CHECK(world.cell_at(0, 7, -4).falling); // both holes receive water
    CHECK(world.cell_at(4, 7, 0).falling);
    CHECK(world.level_at(-1, 8, 0) == 0); // and neither suppressed direction runs
    CHECK(world.level_at(0, 8, 1) == 0);
}

TEST_CASE("fluid: the research document's cliff example") {
    // docs/research/10 §4.6: a source at (2, 2) with a single drop-off at
    // (1, 4). The document's hand-computed direction costs list South as "no
    // drop-off", but §4.4's algorithm reaches the same hole in three steps
    // from the south too (the hole is at Manhattan distance 3 from the
    // source), so the emit set is the tie {West, South} - see the T-F1 report.
    // The observable claim of the example still holds: two of the four
    // directions are suppressed and the water runs to the hole.
    TestFluidWorld world(8);
    for (int y = 4; y < 8; ++y) {
        world.carve(1, y, 4);
    }
    FluidSim sim(world);
    sim.place_source(2, 8, 2);

    const std::uint8_t spread = world.cell_at(2, 8, 2).spread;
    CHECK((spread & voxel::kFluidWest) != 0);
    CHECK((spread & voxel::kFluidSouth) != 0);
    CHECK((spread & voxel::kFluidEast) == 0);
    CHECK((spread & voxel::kFluidNorth) == 0);

    run(sim, 30);
    CHECK(world.cell_at(1, 7, 4).falling); // the water found the cliff
    CHECK(world.level_at(3, 8, 2) == 0);   // and did not fan out
    CHECK(world.level_at(2, 8, 1) == 0);
}

TEST_CASE("fluid: solids block the search as well as the flow") {
    TestFluidWorld world(8);
    // A hole due east, with a wall in the way: the east direction stays INF,
    // and the hole is far enough that no other direction finds it either.
    for (int y = 4; y < 8; ++y) {
        world.carve(4, y, 0);
    }
    world.wall(1, 8, 0);
    FluidSim sim(world);
    sim.place_source(0, 8, 0);

    const std::uint8_t spread = world.cell_at(0, 8, 0).spread;
    CHECK((spread & voxel::kFluidEast) == 0); // blocked outright
    CHECK((spread & voxel::kFluidWest) != 0); // no drop-off found: symmetric
    CHECK((spread & voxel::kFluidNorth) != 0);
    CHECK((spread & voxel::kFluidSouth) != 0);
}

TEST_CASE("fluid: a wall keeps the flow out of a pocket it surrounds") {
    TestFluidWorld world(8);
    // A one-wide pocket at (2, 8, 0) walled off from the source.
    world.wall(1, 8, 0);
    for (int y = 8; y < 10; ++y) {
        world.wall(2, y, 1);
        world.wall(2, y, -1);
    }
    FluidSim sim(world);
    sim.place_source(0, 8, 0);
    run(sim, 40);
    CHECK(world.level_at(2, 8, 0) == 0);
}

// ── lifecycle ───────────────────────────────────────────────────────────────

TEST_CASE("fluid: removing the source drains the whole body") {
    TestFluidWorld world(8);
    FluidSim sim(world);
    sim.place_source(0, 8, 0);
    run(sim, 35);
    CHECK(world.water_cells() > 1);

    sim.clear_cell(0, 8, 0);
    run(sim, 120);
    CHECK(world.water_cells() == 0);
    CHECK(sim.pending() == 0); // and the schedule converges instead of spinning
}

TEST_CASE("fluid: a block placed into water clears the fluid there") {
    TestFluidWorld world(8);
    FluidSim sim(world);
    sim.place_source(0, 8, 0);
    run(sim, 10);
    REQUIRE(world.level_at(1, 8, 0) == 7);

    world.wall(1, 8, 0); // a solid block lands in the flowing cell
    sim.on_block_changed(1, 8, 0);
    run(sim, 10);
    CHECK(world.cell_at(1, 8, 0).empty());
    CHECK(world.level_at(0, 8, 0) == 8); // the source itself survives
    CHECK(world.cell_at(0, 8, 0).source);
    CHECK(world.level_at(-1, 8, 0) == 7); // and the rest of the pool is intact
}

TEST_CASE("fluid: mining a hole under a pool lets the water follow") {
    TestFluidWorld world(8);
    for (int x = -1; x <= 1; ++x) {
        for (int z = -1; z <= 1; ++z) {
            world.carve(x, 4, z); // a cavity under the pool
        }
    }
    FluidSim sim(world);
    sim.place_source(0, 8, 0);
    run(sim, 40);
    CHECK(world.level_at(1, 8, 0) == 7);

    // Open a shaft below the pool's outer ring: the water finds the new
    // drop-off and pours in (docs/research/10 §7.2 block updates).
    for (int y = 4; y < 8; ++y) {
        world.carve(1, y, 0);
    }
    sim.on_block_changed(1, 7, 0);
    run(sim, 40);
    CHECK(world.cell_at(1, 7, 0).kind == FluidKind::Water);
    CHECK(world.cell_at(1, 7, 0).falling); // mid-column
    CHECK(world.level_at(1, 4, 0) == 8);   // the column lands on the cavity floor
}

TEST_CASE("fluid: the scheduled queue dedups positions and caps with a drop counter") {
    TestFluidWorld world(8);
    FluidSim sim(world);
    sim.wake(0, 8, 0);
    sim.wake(0, 8, 0);
    sim.wake(1, 8, 0);          // shares only (0,8,0) with the first wake
    CHECK(sim.pending() == 12); // 14 attempts, 12 distinct positions
    CHECK(sim.dropped() == 0);

    // R-5 / §8.4: past the cap new entries are dropped and counted, never
    // queued (the wiki states the JE cap but not its overflow behaviour).
    for (int x = -200; x <= 200 && sim.dropped() == 0; ++x) {
        for (int z = -200; z <= 200; ++z) {
            sim.wake(x, 40, z);
            if (sim.dropped() != 0) {
                break;
            }
        }
    }
    CHECK(sim.pending() == FluidSim::kMaxPending);
    CHECK(sim.dropped() > 0);
}

TEST_CASE("fluid: the level field is deterministic") {
    auto build = [](TestFluidWorld &world) {
        for (int y = 4; y < 8; ++y) {
            world.carve(2, y, 3);
        }
        FluidSim sim(world);
        sim.place_source(0, 8, 0);
        run(sim, 40);
        std::vector<int> levels;
        for (int x = -8; x <= 8; ++x) {
            for (int z = -8; z <= 8; ++z) {
                levels.push_back(world.level_at(x, 8, z));
            }
        }
        return levels;
    };
    TestFluidWorld first(8);
    TestFluidWorld second(8);
    CHECK(build(first) == build(second));
}

// ── meshing (acceptance 2/5, rendering half) ────────────────────────────────

namespace {

std::int64_t pack_pos(int x, int y, int z) {
    return (static_cast<std::int64_t>(x) << 40) | (static_cast<std::int64_t>(z) << 20) | y;
}

class TestBlockSource final : public render::IBlockSource {
public:
    std::unordered_map<std::int64_t, std::uint16_t> overrides;

    [[nodiscard]] std::uint16_t block_at(int wx, int wy, int wz) const override {
        if (wy < 0 || wy >= render::kChunkSizeY) {
            return 0;
        }
        const auto it = overrides.find(pack_pos(wx, wy, wz));
        return it == overrides.end() ? 0 : it->second;
    }
};

class TestFluidSource final : public render::IFluidSource {
public:
    std::unordered_map<std::int64_t, float> heights;

    [[nodiscard]] float fluid_height_at(int wx, int wy, int wz) const override {
        const auto it = heights.find(pack_pos(wx, wy, wz));
        return it == heights.end() ? 0.0f : it->second;
    }
};

constexpr std::uint16_t kWaterBlock = 12;
constexpr std::uint16_t kStoneBlock = 3;

} // namespace

TEST_CASE("mesher: a fluid surface renders at the level height, not at full block") {
    TestBlockSource blocks;
    TestFluidSource fluid;
    blocks.overrides.emplace(pack_pos(0, 0, 0), kStoneBlock);
    blocks.overrides.emplace(pack_pos(0, 1, 0), kWaterBlock);
    fluid.heights.emplace(pack_pos(0, 1, 0), 3.0f / 9.0f); // level 3

    const render::MeshData mesh = render::build_chunk_mesh(blocks, {0, 0}, &fluid);
    CHECK(mesh.translucent.vertices.empty());  // the water cube is replaced
    CHECK(mesh.fluid.indices.size() / 6 == 5); // top + four side walls

    float min_y = 1e9f;
    float max_y = -1e9f;
    for (const render::FluidVertex &v : mesh.fluid.vertices) {
        min_y = std::min(min_y, v.y);
        max_y = std::max(max_y, v.y);
    }
    CHECK(min_y == 1.0f); // the cell floor (block y = 1)
    CHECK(std::abs(max_y - (1.0f + 3.0f / 9.0f)) < 1e-5f);
    CHECK(mesh.fluid.vertices.front().tile == render::tile_index(kWaterBlock, 0));
}

TEST_CASE("mesher: a level step between two water cells becomes a visible wall") {
    TestBlockSource blocks;
    TestFluidSource fluid;
    blocks.overrides.emplace(pack_pos(0, 1, 0), kWaterBlock);
    blocks.overrides.emplace(pack_pos(1, 1, 0), kWaterBlock);
    fluid.heights.emplace(pack_pos(0, 1, 0), 1.0f);        // source: full height
    fluid.heights.emplace(pack_pos(1, 1, 0), 3.0f / 9.0f); // level 3

    const render::MeshData mesh = render::build_chunk_mesh(blocks, {0, 0}, &fluid);
    CHECK(mesh.translucent.vertices.empty());

    // Both cells emit a top surface, and exactly one wall sits in the band
    // between the two levels (lower edge at the shallow surface, upper edge at
    // the full one).
    std::size_t at_full = 0;
    std::size_t at_shallow = 0;
    for (const render::FluidVertex &v : mesh.fluid.vertices) {
        if (v.y == 2.0f) {
            ++at_full;
        }
        if (std::abs(v.y - (1.0f + 3.0f / 9.0f)) < 1e-5f) {
            ++at_shallow;
        }
    }
    // Each band collects 2 vertices per wall edge touching it: the four walls
    // of the full cell and the three exposed walls of the shallow one, plus
    // the top quads (2 + 2 each on the shared wall, 4 + 4 on the tops).
    CHECK(at_full == 4 + 2 + 2 + 2 + 2);
    CHECK(at_shallow == 4 + 2 + 2 + 2 + 2);
}

TEST_CASE("mesher: worldgen water without a fluid surface keeps meshing as a cube") {
    TestBlockSource blocks;
    TestFluidSource fluid; // no heights at all
    blocks.overrides.emplace(pack_pos(0, 0, 0), kWaterBlock);
    const render::MeshData mesh = render::build_chunk_mesh(blocks, {0, 0}, &fluid);
    CHECK(mesh.fluid.vertices.empty());
    CHECK(mesh.translucent.indices.size() / 6 == 5); // unchanged T005 behaviour

    // Same result with no fluid source at all (every pre-T-F1 caller).
    const render::MeshData legacy = render::build_chunk_mesh(blocks, {0, 0});
    CHECK(legacy == mesh);
}

TEST_CASE("mesher: level 8 renders a full block and level 1 the thinnest shell") {
    TestBlockSource blocks;
    TestFluidSource fluid;
    blocks.overrides.emplace(pack_pos(0, 1, 0), kWaterBlock);
    blocks.overrides.emplace(pack_pos(0, 2, 0), kWaterBlock);

    fluid.heights.emplace(pack_pos(0, 1, 0), voxel::fluid_render_height(8));
    fluid.heights.emplace(pack_pos(0, 2, 0), voxel::fluid_render_height(1));
    CHECK(voxel::fluid_render_height(8) == 1.0f);
    CHECK(std::abs(voxel::fluid_render_height(1) - 1.0f / 9.0f) < 1e-6f);

    const render::MeshData mesh = render::build_chunk_mesh(blocks, {0, 0}, &fluid);
    // The stacked cell above the full one suppresses the lower cell's top
    // surface (the water is one body), leaving exactly one top in the mesh.
    std::size_t tops = 0;
    for (std::size_t i = 0; i + 3 < mesh.fluid.vertices.size(); i += 4) {
        bool is_top = true;
        for (std::size_t c = 1; c < 4; ++c) {
            if (mesh.fluid.vertices[i + c].y != mesh.fluid.vertices[i].y) {
                is_top = false;
            }
        }
        if (is_top) {
            ++tops;
        }
    }
    CHECK(tops == 1); // only the level-1 cell on top of the stack
}
