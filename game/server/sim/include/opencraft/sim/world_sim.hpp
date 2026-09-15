#pragma once

// T-A1: the authoritative side. WorldSim owns the world's mutable state
// (chunks, light, fluid) and is the ONLY place a block or a fluid cell is
// written - the block writers are private, so "single writer" is enforced by
// the access specifier rather than by convention.
//
// The client's boundary is:
//   * reads      -> client::WorldSource, a const view over this object;
//   * actions    -> game::IAuthority::submit() (this class), validated here;
//   * push-back  -> take_changes(), the stale-chunk list the client remeshes.
//
// The class runs inside the client process for now (docs/03 §1: 客户端+内嵌
// 服务端); M3 puts a network channel in front of the same three verbs, which
// is why the request/result types live in game/common and carry plain data.

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "opencraft/game/protocol.hpp"
#include "opencraft/render/mesher.hpp"
#include "opencraft/storage/world_save.hpp"
#include "opencraft/voxel/block_registry.hpp"
#include "opencraft/voxel/chunk_manager.hpp"
#include "opencraft/voxel/fluid_sim.hpp"
#include "opencraft/voxel/light_engine.hpp"
#include "opencraft/voxel/light_world_adapter.hpp"
#include "opencraft/worldgen/terrain_generator.hpp"

namespace opencraft::server {

// The world simulation: ChunkManager + TerrainGenerator (T004) + LightEngine
// (T006) + FluidSim (T-F1) wired together, plus the action rules that gate
// every write. It answers the same read queries the client's renderer,
// physics, raycasting and HUD need (render::IBlockSource and friends).
//
// Coordinate convention (unchanged from T004): CHUNK STORAGE coordinates -
// block (wx, wy, wz) addresses Chunk::get_block(lx, wy, lz) directly, so the
// world y span is [0, 384).
class WorldSim final : public game::IAuthority, public render::IBlockSource {
public:
    // "OPENCRAFT" as hex - the same seed the worldgen golden file pins, so
    // the spawn area is the known grass-under-air chunk. Used for new worlds;
    // a loaded level.ocd overrides it with the persisted seed.
    static constexpr std::uint64_t kSeed = 0x4F50454E43524146ULL;

    // World block height in Chunk storage coordinates.
    static constexpr int kWorldHeight = voxel::Chunk::kSizeY;

    explicit WorldSim(std::uint64_t seed = kSeed);

    // The fluid simulation holds a pointer back into this object (FluidWorld
    // below), so a copy would silently point at the original's world. Neither
    // copy nor move is meaningful for a simulation that owns its state.
    WorldSim(const WorldSim &) = delete;
    WorldSim &operator=(const WorldSim &) = delete;
    WorldSim(WorldSim &&) = delete;
    WorldSim &operator=(WorldSim &&) = delete;

    // ── the command channel (game::IAuthority) ──────────────────────────────
    // Validates and executes one request. Every rejection is a rule this
    // class owns; the client is not allowed to pre-empt or duplicate them.
    [[nodiscard]] game::ActionResult submit(const game::ActionRequest &req) override;

    // One authoritative 20 TPS step: the fluid scheduled-tick queue advances
    // and the chunks it made stale are queued for push-back.
    void tick() override;

    // Hands over (and empties) everything that changed since the last call.
    // Callers drain once per tick; a caller that never drains grows this
    // buffer without bound.
    [[nodiscard]] game::WorldChanges take_changes() override;

    // ── world lifecycle (chunk streaming + persistence) ─────────────────────
    // Persistence hook (T009). Nullopt = pure in-memory world (tests, prior
    // behavior). The save is NOT owned; it must outlive the WorldSim.
    void attach_save(storage::WorldSave *save) { save_ = save; }

    // Generates (if absent), light-initializes and stores chunk (cx, cz).
    // Disk-first: when a save is attached and the chunk exists on disk, the
    // persisted blocks win over regeneration (light is recomputed - the
    // documented T009 choice). Returns true when the chunk was newly created
    // or loaded this call. Synchronous; the caller owns the frame budget.
    bool ensure_chunk(int cx, int cz);

    [[nodiscard]] bool chunk_ready(int cx, int cz) const { return chunks_.find(cx, cz) != nullptr; }

    // True when the chunk and its 4 side neighbors are generated - meshing a
    // chunk before that would bake wrong (air) border faces.
    [[nodiscard]] bool neighbors_ready(int cx, int cz) const;

    // Topmost solid block of the column + 1 (the feet y to spawn at); 0 when
    // the column is empty.
    [[nodiscard]] int surface_height(int wx, int wz) const;

    // 5x5 spawn scan around the origin (T009): first column closest to the
    // center whose surface block is solid, with two air cells above it, wins.
    // Returns the feet-center spawn position (x+0.5, surface_y, z+0.5).
    [[nodiscard]] glm::dvec3 find_spawn();

    // Unload hook (T006 follow-up, wired by T009): serializes the chunk to
    // the attached save first (if any), then drops its light storage and the
    // block data. Returns true when a chunk was unloaded.
    bool unload_chunk(int cx, int cz);

    // Autosave pass (call once per cadence window from the main thread):
    // drains the save's dirty set and submits serialized snapshots of every
    // still-loaded chunk to the save's IO thread (bytes copied here - the IO
    // thread never touches chunk state, per the T009 card). Returns how many
    // chunks were handed to the writer.
    std::size_t autosave_pass() override;

    // Serializes one loaded chunk into `out` (Chunk format v2; v1 payloads
    // deserialize to an empty fluid layer). Returns false when the chunk is
    // not loaded.
    [[nodiscard]] bool serialize_chunk(int cx, int cz, core::ByteBuffer &out) const;

    // ── reads (renderer, physics, raycast, HUD, action validation) ──────────
    [[nodiscard]] const voxel::BlockRegistry &registry() const { return registry_; }

    [[nodiscard]] const voxel::LightEngine &light() const { return light_; }

    // Block id the fluid layer uses as its in-world placeholder.
    [[nodiscard]] std::uint16_t water_block_id() const { return water_block_id_; }

    [[nodiscard]] std::uint16_t block_at(int wx, int wy, int wz) const override;

    // physics::IBlockSource contract: unloaded chunks read as solid, so the
    // player cannot walk/fall into the streaming void.
    [[nodiscard]] bool solid_at(int wx, int wy, int wz) const;

    [[nodiscard]] bool liquid_at(int wx, int wy, int wz) const;

    // Water surface height for the mesher (render::IFluidSource). 0 for a cell
    // without fluid; worldgen water is not in the fluid layer and therefore
    // keeps meshing as a full cube.
    [[nodiscard]] float fluid_height_at(int wx, int wy, int wz) const;

    [[nodiscard]] render::FluidSpan fluid_span(int cx, int cz) const;

    // Packed fluid cell (voxel::IFluidWorld contract), 0 when there is none.
    [[nodiscard]] std::uint16_t fluid_at(int wx, int wy, int wz) const;

    // True when a fluid may occupy this position: air, or a replaceable
    // (liquid) block. Unloaded chunks and out-of-world y answer false, so the
    // simulation treats them as walls (docs/research/10 §4.7, R-5).
    [[nodiscard]] bool fluid_may_enter(int wx, int wy, int wz) const;

    // True when a bucket can pick this cell up: a fluid-layer source, or a
    // legacy static water block (a worldgen ocean counts, matching MC).
    [[nodiscard]] bool is_water_source(int wx, int wy, int wz) const;

private:
    // ── the writers. Only submit() and the fluid simulation reach these. ────

    // Writes a block and updates light incrementally. Appends every chunk
    // whose mesh may have changed to the pending push-back (own chunk plus
    // side neighbors within light range of the changed cell). No-op when the
    // chunk is not loaded. The owning chunk is also marked dirty in the
    // attached save (only its DATA changed).
    void write_block(int wx, int wy, int wz, std::uint16_t id);

    // Marks the chunks whose baked mesh the changed cell invalidates. Called
    // by the fluid layer through set_fluid_at(); the fluid simulation names the
    // same chunk many times per tick, so the list is deduplicated in tick().
    void mark_fluid_dirty(int wx, int wz);

    // Fluid layer write path (voxel::IFluidWorld::set_fluid_at). Private: the
    // only caller is FluidWorld below.
    void set_fluid_at(int wx, int wy, int wz, std::uint16_t cell);

    // Places a water source and lets the simulation spread from there. False
    // when the target chunk is not loaded.
    bool place_water_source(int wx, int wy, int wz);

    // Removes the water source at the position (bucket pickup). True when
    // there was one: either a fluid-layer source or a legacy static water
    // block (a worldgen ocean counts as a source, matching what a bucket can
    // pick up in MC).
    bool remove_water_source(int wx, int wy, int wz);

    // ── action rules ────────────────────────────────────────────────────────
    // Shared gate: target inside the world, in a loaded chunk, inside ⚖ reach.
    [[nodiscard]] game::ActionReject gate(const game::ActionRequest &req) const;
    [[nodiscard]] bool in_reach(const game::ActionRequest &req) const;

    [[nodiscard]] game::ActionResult apply_dig(const game::ActionRequest &req);
    [[nodiscard]] game::ActionResult apply_place(const game::ActionRequest &req);
    [[nodiscard]] game::ActionResult apply_pour(const game::ActionRequest &req);
    [[nodiscard]] game::ActionResult apply_scoop(const game::ActionRequest &req);

    // Re-evaluates the fluid around a chunk that just entered memory: the new
    // chunk holds fluid no neighbour has seen yet, and the neighbours' border
    // columns face a cell that used to be unloaded (docs/research/10 §7.2 -
    // generated chunks do not push block updates).
    void wake_fluid_at_chunk_border(int cx, int cz);

    // FluidSim's view of the world. A private nested type so the fluid write
    // channel stays inside the authority while IFluidWorld itself stays a
    // public engine interface (nested classes share the enclosing access).
    class FluidWorld final : public voxel::IFluidWorld {
    public:
        explicit FluidWorld(WorldSim &sim) : sim_(&sim) {}

        [[nodiscard]] std::uint16_t fluid_at(int wx, int wy, int wz) const override {
            return sim_->fluid_at(wx, wy, wz);
        }

        void set_fluid_at(int wx, int wy, int wz, std::uint16_t cell) override { sim_->set_fluid_at(wx, wy, wz, cell); }

        [[nodiscard]] bool fluid_may_enter(int wx, int wy, int wz) const override {
            return sim_->fluid_may_enter(wx, wy, wz);
        }

    private:
        WorldSim *sim_;
    };

    voxel::BlockRegistry registry_;
    std::uint16_t water_block_id_ = 0;
    voxel::ChunkManager chunks_;
    voxel::ChunkLightWorld light_world_;
    voxel::LightEngine light_;
    worldgen::TerrainGenerator generator_;
    FluidWorld fluid_world_;
    voxel::FluidSim fluid_;
    // Chunks the fluid layer made stale; flushed into pending_chunks_ by tick().
    std::vector<std::pair<int, int>> fluid_dirty_;
    // Push-back accumulated since the last take_changes().
    std::vector<std::pair<int, int>> pending_chunks_;
    storage::WorldSave *save_ = nullptr;
};

} // namespace opencraft::server
