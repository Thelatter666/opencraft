#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "opencraft/game/raycast.hpp"
#include "opencraft/physics/block_source.hpp"
#include "opencraft/render/mesher.hpp"
#include "opencraft/storage/world_save.hpp"
#include "opencraft/voxel/block_registry.hpp"
#include "opencraft/voxel/chunk_manager.hpp"
#include "opencraft/voxel/fluid_sim.hpp"
#include "opencraft/voxel/light_engine.hpp"
#include "opencraft/voxel/light_world_adapter.hpp"
#include "opencraft/worldgen/terrain_generator.hpp"

namespace opencraft::client {

// Client-side world: ChunkManager + TerrainGenerator (T004) + LightEngine
// (T006) + FluidSim (T-F1) wired together, plus the adapter interfaces the
// renderer, physics and the fluid simulation ask through (block queries for
// meshing/raycasting, solid/liquid queries for step_player, fluid surfaces for
// the water layer).
//
// Coordinate convention: the client plays in CHUNK STORAGE coordinates -
// block (wx, wy, wz) addresses Chunk::get_block(lx, wy, lz) directly, so the
// world y span is [0, 384). (T004's generator maps local y to doc-world
// y-64; that offset only matters for disk persistence, which is T009.)
class WorldSource final : public render::IBlockSource,
                          public physics::IBlockSource,
                          public render::IFluidSource,
                          public voxel::IFluidWorld {
public:
    // "OPENCRAFT" as hex - the same seed the worldgen golden file pins, so
    // the spawn area is the known grass-under-air chunk. Used for new worlds;
    // a loaded level.ocd overrides it with the persisted seed.
    static constexpr std::uint64_t kSeed = 0x4F50454E43524146ULL;

    // World block height in Chunk storage coordinates.
    static constexpr int kWorldHeight = voxel::Chunk::kSizeY;

    explicit WorldSource(std::uint64_t seed = kSeed);

    [[nodiscard]] voxel::BlockRegistry &registry() { return registry_; }

    [[nodiscard]] const voxel::BlockRegistry &registry() const { return registry_; }

    [[nodiscard]] voxel::FluidSim &fluid() { return fluid_; }

    [[nodiscard]] const voxel::FluidSim &fluid() const { return fluid_; }

    // Block id the fluid layer uses as its in-world placeholder.
    [[nodiscard]] std::uint16_t water_block_id() const { return water_block_id_; }

    [[nodiscard]] voxel::LightEngine &light() { return light_; }

    [[nodiscard]] const voxel::LightEngine &light() const { return light_; }

    // Persistence hook (T009). Nullopt = pure in-memory world (tests, prior
    // behavior). The save is NOT owned; it must outlive the WorldSource.
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

    // Writes a block and updates light incrementally. Appends every chunk
    // whose mesh may have changed to `dirty` (own chunk plus side neighbors
    // within light range of the changed cell). The owning chunk is also
    // marked dirty in the attached save (only its DATA changed). No-op when
    // the chunk is not loaded.
    void set_block(int wx, int wy, int wz, std::uint16_t id, std::vector<std::pair<int, int>> &dirty);

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
    std::size_t autosave_pass();

    // Serializes one loaded chunk into `out` (Chunk format v2; v1 payloads
    // deserialize to an empty fluid layer). Returns false when the chunk is
    // not loaded.
    [[nodiscard]] bool serialize_chunk(int cx, int cz, core::ByteBuffer &out) const;

    // --- fluid simulation (T-F1) ---------------------------------------------
    // Advances the fluid scheduled-tick queue by one game tick (call once per
    // logic tick) and appends every chunk whose mesh is now stale to `dirty`.
    void fluid_step(std::vector<std::pair<int, int>> &dirty);

    // Places a water source and lets the simulation spread it from there.
    // False when the target chunk is not loaded.
    bool place_water_source(int wx, int wy, int wz);

    // Removes the water source at the position (bucket pickup). True when
    // there was one: either a fluid-layer source or a legacy static water
    // block (a worldgen ocean counts as a source, matching what a bucket can
    // pick up in MC).
    bool remove_water_source(int wx, int wy, int wz, std::vector<std::pair<int, int>> &dirty);

    [[nodiscard]] bool is_water_source(int wx, int wy, int wz) const;

    // --- render::IBlockSource (meshing + voxel raycast) ----------------------
    [[nodiscard]] std::uint16_t block_at(int wx, int wy, int wz) const override;

    // --- render::IFluidSource (water surface height for the mesher) ----------
    // 0 for a cell without fluid; worldgen water is not in the fluid layer and
    // therefore keeps meshing as a full cube.
    [[nodiscard]] float fluid_height_at(int wx, int wy, int wz) const override;
    [[nodiscard]] render::FluidSpan fluid_span(int cx, int cz) const override;

    // --- voxel::IFluidWorld (the simulation's world view) --------------------
    [[nodiscard]] std::uint16_t fluid_at(int wx, int wy, int wz) const override;
    void set_fluid_at(int wx, int wy, int wz, std::uint16_t cell) override;
    [[nodiscard]] bool fluid_may_enter(int wx, int wy, int wz) const override;

    // --- physics::IBlockSource (step_player) ---------------------------------
    // Unloaded chunks read as solid: the player cannot walk/fall into the
    // streaming void.
    [[nodiscard]] bool solid_at(int wx, int wy, int wz) const override;
    [[nodiscard]] bool liquid_at(int wx, int wy, int wz) const override;

private:
    void mark_mesh_dirty(int wx, int wz);
    // Re-evaluates the fluid around a chunk that just entered memory: the new
    // chunk holds fluid no neighbour has seen yet, and the neighbours' border
    // columns face a cell that used to be unloaded (docs/research/10 §7.2 -
    // generated chunks do not push block updates).
    void wake_fluid_at_chunk_border(int cx, int cz);

    voxel::BlockRegistry registry_;
    std::uint16_t water_block_id_ = 0;
    voxel::ChunkManager chunks_;
    voxel::ChunkLightWorld light_world_;
    voxel::LightEngine light_;
    worldgen::TerrainGenerator generator_;
    voxel::FluidSim fluid_;
    std::vector<std::pair<int, int>> fluid_dirty_;
    storage::WorldSave *save_ = nullptr;
};

// Applies the T008 lighting rule to a freshly built mesh: per face-quad,
// sample sky/block light at the air cell the face opens into and fold it
// into the T005 directional shade byte:
//     brightness = max(sky * day_factor, block) / 15, day_factor = 1.0
// with a small ambient floor so caves stay barely visible. Quad normals are
// recovered from the CCW winding, so the mesher stays untouched.
void shade_mesh_with_light(render::MeshData &mesh, const WorldSource &world, int cx, int cz);

} // namespace opencraft::client
