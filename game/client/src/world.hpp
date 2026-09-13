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
#include "opencraft/voxel/light_engine.hpp"
#include "opencraft/voxel/light_world_adapter.hpp"
#include "opencraft/worldgen/terrain_generator.hpp"

namespace opencraft::client {

// Client-side world: ChunkManager + TerrainGenerator (T004) + LightEngine
// (T006) wired together, plus the two IBlockSource adapters (render block
// queries for meshing/raycasting, physics solid/liquid queries for
// step_player).
//
// Coordinate convention: the client plays in CHUNK STORAGE coordinates -
// block (wx, wy, wz) addresses Chunk::get_block(lx, wy, lz) directly, so the
// world y span is [0, 384). (T004's generator maps local y to doc-world
// y-64; that offset only matters for disk persistence, which is T009.)
class WorldSource final : public render::IBlockSource, public physics::IBlockSource {
public:
    // "OPENCRAFT" as hex - the same seed the worldgen golden file pins, so
    // the spawn area is the known grass-under-air chunk. Used for new worlds;
    // a loaded level.ocd overrides it with the persisted seed.
    static constexpr std::uint64_t kSeed = 0x4F50454E43524146ULL;

    explicit WorldSource(std::uint64_t seed = kSeed);

    [[nodiscard]] voxel::BlockRegistry &registry() { return registry_; }

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
    // thread never touches chunk state, per the T009 card).
    void autosave_pass();

    // Serializes one loaded chunk into `out` (Chunk format v1). Returns false
    // when the chunk is not loaded.
    [[nodiscard]] bool serialize_chunk(int cx, int cz, core::ByteBuffer &out) const;

    // --- render::IBlockSource (meshing + voxel raycast) ----------------------
    [[nodiscard]] std::uint16_t block_at(int wx, int wy, int wz) const override;

    // --- physics::IBlockSource (step_player) ---------------------------------
    // Unloaded chunks read as solid: the player cannot walk/fall into the
    // streaming void.
    [[nodiscard]] bool solid_at(int wx, int wy, int wz) const override;
    [[nodiscard]] bool liquid_at(int wx, int wy, int wz) const override;

private:
    voxel::BlockRegistry registry_;
    voxel::ChunkManager chunks_;
    voxel::ChunkLightWorld light_world_;
    voxel::LightEngine light_;
    worldgen::TerrainGenerator generator_;
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
