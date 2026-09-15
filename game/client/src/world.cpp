#include "world.hpp"

#include "opencraft/core/log.hpp"
#include "opencraft/worldgen/terrain_generator.hpp"

#include <algorithm>
#include <cmath>

namespace opencraft::client {

namespace {

// Visits every fluid cell of a chunk, skipping sections without fluid storage.
template <typename Fn>
void for_each_fluid_cell(const voxel::Chunk &chunk, Fn &&visit) {
    for (int section = 0; section < voxel::Chunk::kSectionCount; ++section) {
        if (chunk.fluid_section_empty(section)) {
            continue;
        }
        const int base_y = section * voxel::Chunk::kSectionSize;
        for (int ly = 0; ly < voxel::Chunk::kSectionSize; ++ly) {
            for (int lz = 0; lz < voxel::Chunk::kSizeZ; ++lz) {
                for (int lx = 0; lx < voxel::Chunk::kSizeX; ++lx) {
                    if (!chunk.get_fluid(lx, base_y + ly, lz).empty()) {
                        visit(lx, base_y + ly, lz);
                    }
                }
            }
        }
    }
}

} // namespace

WorldSource::WorldSource(const std::uint64_t seed)
    : registry_(voxel::BlockRegistry::create_default()), light_world_(chunks_, registry_, {}), light_(light_world_),
      generator_(seed, registry_), fluid_(*this) {
    water_block_id_ = registry_.id_of("water");
}

bool WorldSource::ensure_chunk(int cx, int cz) {
    voxel::Chunk &chunk = chunks_.get_or_load(cx, cz);
    // Detect a fresh chunk: get_or_load hands out an empty one when absent.
    // (A generated chunk is never empty - bedrock floor is always present.)
    if (!chunk.empty()) {
        return false;
    }
    // Disk-first (T009): persisted blocks win over regeneration. Light is
    // NOT persisted (the documented choice) - init_chunk recomputes it.
    if (save_ != nullptr) {
        if (const std::optional<std::vector<std::uint8_t>> payload = save_->load_chunk(cx, cz); payload.has_value()) {
            core::ByteBuffer buffer;
            buffer.write_bytes(payload->data(), payload->size());
            buffer.rewind();
            chunk = voxel::Chunk::deserialize(buffer);
            light_.init_chunk(cx, cz);
            wake_fluid_at_chunk_border(cx, cz);
            OC_LOG_INFO("chunk ({}, {}) loaded from disk", cx, cz);
            return true;
        }
    }
    generator_.generate_chunk(cx, cz, chunk);
    light_.init_chunk(cx, cz);
    wake_fluid_at_chunk_border(cx, cz);
    return true;
}

bool WorldSource::neighbors_ready(int cx, int cz) const {
    return chunk_ready(cx, cz) && chunk_ready(cx - 1, cz) && chunk_ready(cx + 1, cz) && chunk_ready(cx, cz - 1) &&
           chunk_ready(cx, cz + 1);
}

void WorldSource::set_block(int wx, int wy, int wz, std::uint16_t id, std::vector<std::pair<int, int>> &dirty) {
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return;
    }
    const int lx = wx - cx * voxel::Chunk::kSizeX;
    const int lz = wz - cz * voxel::Chunk::kSizeZ;
    const std::uint16_t old_id = chunk->get_block(lx, wy, lz);
    if (old_id == id) {
        return;
    }
    chunk->set_block(lx, wy, lz, id);
    light_.on_block_changed(wx, wy, wz, old_id, id);
    // A block edit is a block update for the fluid too: the cell and its
    // neighbours re-evaluate on the fluid's own schedule, which is what lets
    // water pour into a freshly mined hole (docs/research/10 §7.2).
    fluid_.on_block_changed(wx, wy, wz);
    if (save_ != nullptr) {
        save_->mark_dirty(cx, cz);
    }

    // Own chunk plus every side neighbor within light reach of the changed
    // cell (light spreads up to 15 cells horizontally, so a change at lx
    // can re-shade faces in chunk cx+1 unless lx == 15 - the light cannot
    // cross 16 cells). Diagonal neighbors when both axes are in reach.
    auto mark = [&dirty](int x, int z) { dirty.emplace_back(x, z); };
    mark(cx, cz);
    const bool east = lx >= 1;
    const bool west = lx <= voxel::Chunk::kSizeX - 2;
    const bool south = lz >= 1;
    const bool north = lz <= voxel::Chunk::kSizeZ - 2;
    if (east) {
        mark(cx + 1, cz);
    }
    if (west) {
        mark(cx - 1, cz);
    }
    if (south) {
        mark(cx, cz + 1);
    }
    if (north) {
        mark(cx, cz - 1);
    }
    if (east && south) {
        mark(cx + 1, cz + 1);
    }
    if (east && north) {
        mark(cx + 1, cz - 1);
    }
    if (west && south) {
        mark(cx - 1, cz + 1);
    }
    if (west && north) {
        mark(cx - 1, cz - 1);
    }
}

int WorldSource::surface_height(int wx, int wz) const {
    const voxel::Chunk *chunk = chunks_.find_world(wx, wz);
    if (chunk == nullptr) {
        return 0;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const int lx = wx - cx * voxel::Chunk::kSizeX;
    const int lz = wz - cz * voxel::Chunk::kSizeZ;
    for (int y = voxel::Chunk::kSizeY - 1; y >= 0; --y) {
        const std::uint16_t id = chunk->get_block(lx, y, lz);
        if (id != 0 && registry_.def_of(id).solid) {
            return y + 1;
        }
    }
    return 0;
}

glm::dvec3 WorldSource::find_spawn() {
    // 5x5 column scan around the origin, center-out (T009 card item: spawn
    // on the surface's highest solid block). All 25 columns live in chunk
    // (0, 0), which the caller generated first.
    static constexpr std::pair<int, int> kOffsets[25] = {
        {0, 0},  {1, 0},  {-1, 0},  {0, 1},  {0, -1}, {1, 1},  {1, -1},  {-1, 1},  {-1, -1},
        {2, 0},  {-2, 0}, {0, 2},   {0, -2}, {2, 1},  {2, -1}, {-2, 1},  {-2, -1}, {1, 2},
        {-1, 2}, {1, -2}, {-1, -2}, {2, 2},  {2, -2}, {-2, 2}, {-2, -2},
    };
    for (const auto &[dx, dz] : kOffsets) {
        const int wx = dx;
        const int wz = dz;
        const int surface = surface_height(wx, wz);
        if (surface <= 0 || surface + 1 >= voxel::Chunk::kSizeY) {
            continue;
        }
        const std::uint16_t ground = block_at(wx, surface - 1, wz);
        if (!registry_.def_of(ground).solid || registry_.def_of(ground).liquid) {
            continue;
        }
        // Two air cells above the ground: feet and head.
        if (block_at(wx, surface, wz) != 0 || block_at(wx, surface + 1, wz) != 0) {
            continue;
        }
        return {wx + 0.5, static_cast<double>(surface), wz + 0.5};
    }
    // Scan failed (e.g. the whole area is water): fall back to the legacy
    // spawn column; physics will handle whatever is there.
    return {8.5, static_cast<double>(surface_height(8, 8)), 8.5};
}

bool WorldSource::unload_chunk(int cx, int cz) {
    if (chunks_.find(cx, cz) == nullptr) {
        return false;
    }
    // Persistence before release (T009 card: 卸载时若有脏数据先落盘再释放).
    // Untouched chunks regenerate deterministically, so only modified ones
    // hit the disk (card: 未修改的区块不落盘).
    if (save_ != nullptr && save_->is_dirty(cx, cz)) {
        core::ByteBuffer payload;
        if (serialize_chunk(cx, cz, payload)) {
            save_->store_chunk_sync(cx, cz, payload.data(), payload.size());
        }
    }
    light_.forget_chunk(cx, cz);
    static_cast<void>(chunks_.unload(voxel::Chunk::chunk_coord(cx * voxel::Chunk::kSizeX, cz * voxel::Chunk::kSizeZ)));
    return true;
}

std::size_t WorldSource::autosave_pass() {
    if (save_ == nullptr) {
        return 0;
    }
    std::size_t written = 0;
    for (const auto &[cx, cz] : save_->take_dirty()) {
        core::ByteBuffer payload;
        if (serialize_chunk(cx, cz, payload)) {
            // Snapshot copied on the main thread; the IO thread only ever
            // sees the byte vector (T009 card concurrency rule).
            save_->store_chunk_async(cx, cz,
                                     std::vector<std::uint8_t>(payload.data(), payload.data() + payload.size()));
            ++written;
        }
        // A dirty-but-unloaded chunk (should not happen: set_block only marks
        // loaded chunks) is simply dropped from the set; its data is whatever
        // disk already holds.
    }
    return written;
}

bool WorldSource::serialize_chunk(int cx, int cz, core::ByteBuffer &out) const {
    const voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return false;
    }
    chunk->serialize(out);
    return true;
}

std::uint16_t WorldSource::block_at(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return 0; // out of world: air (render IBlockSource contract)
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return 0; // unloaded: air, keeps world edges visible
    }
    return chunk->get_block(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ);
}

bool WorldSource::solid_at(int wx, int wy, int wz) const {
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return true; // unloaded reads solid: no falling into the void
    }
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return false;
    }
    return registry_.def_of(chunk->get_block(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ)).solid;
}

bool WorldSource::liquid_at(int wx, int wy, int wz) const {
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr || wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return false;
    }
    return registry_.def_of(chunk->get_block(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ))
        .liquid;
}

// --- fluid layer (T-F1) -----------------------------------------------------

float WorldSource::fluid_height_at(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return 0.0f;
    }
    const voxel::Chunk *chunk = chunks_.find_world(wx, wz);
    if (chunk == nullptr) {
        return 0.0f;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const voxel::FluidCell cell = chunk->get_fluid(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ);
    if (cell.empty()) {
        return 0.0f;
    }
    return voxel::fluid_render_height(cell.level);
}

std::uint16_t WorldSource::fluid_at(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return 0;
    }
    const voxel::Chunk *chunk = chunks_.find_world(wx, wz);
    if (chunk == nullptr) {
        return 0;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    return voxel::pack_fluid(chunk->get_fluid(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ));
}

void WorldSource::set_fluid_at(int wx, int wy, int wz, std::uint16_t cell) {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return; // unloaded: the simulation treats it as a wall anyway
    }
    const int lx = wx - cx * voxel::Chunk::kSizeX;
    const int lz = wz - cz * voxel::Chunk::kSizeZ;
    const voxel::FluidCell unpacked = voxel::unpack_fluid(cell);
    chunk->set_fluid(lx, wy, lz, unpacked);

    // Keep the block layer in step so targeting, mining, physics and
    // persistence all see the water. A cell that loses its fluid only reverts
    // to air when it actually held the placeholder block (a player-placed
    // block on top of water must not be deleted).
    const std::uint16_t old_id = chunk->get_block(lx, wy, lz);
    const std::uint16_t new_id = !unpacked.empty() ? water_block_id_ : (old_id == water_block_id_ ? 0 : old_id);
    if (old_id != new_id) {
        chunk->set_block(lx, wy, lz, new_id);
        light_.on_block_changed(wx, wy, wz, old_id, new_id);
    }
    if (save_ != nullptr) {
        save_->mark_dirty(cx, cz);
    }
    mark_mesh_dirty(wx, wz);
}

bool WorldSource::fluid_may_enter(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return false;
    }
    const voxel::Chunk *chunk = chunks_.find_world(wx, wz);
    if (chunk == nullptr) {
        return false; // unloaded chunks are walls for fluid (§4.7 / R-5)
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const std::uint16_t id = chunk->get_block(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ);
    if (id == 0) {
        return true; // air
    }
    return registry_.has_numeric(id) && registry_.def_of(id).liquid;
}

void WorldSource::mark_mesh_dirty(int wx, int wz) {
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const int lx = wx - cx * voxel::Chunk::kSizeX;
    const int lz = wz - cz * voxel::Chunk::kSizeZ;
    fluid_dirty_.emplace_back(cx, cz);
    // A cell on a chunk border also changes the neighbour chunk's mesh: the
    // water surface of the neighbour emits a wall against it.
    if (lx == 0) {
        fluid_dirty_.emplace_back(cx - 1, cz);
    }
    if (lx == voxel::Chunk::kSizeX - 1) {
        fluid_dirty_.emplace_back(cx + 1, cz);
    }
    if (lz == 0) {
        fluid_dirty_.emplace_back(cx, cz - 1);
    }
    if (lz == voxel::Chunk::kSizeZ - 1) {
        fluid_dirty_.emplace_back(cx, cz + 1);
    }
}

void WorldSource::wake_fluid_at_chunk_border(int cx, int cz) {
    if (const voxel::Chunk *chunk = chunks_.find(cx, cz); chunk != nullptr) {
        for_each_fluid_cell(*chunk, [&](int lx, int y, int lz) {
            fluid_.wake(cx * voxel::Chunk::kSizeX + lx, y, cz * voxel::Chunk::kSizeZ + lz);
        });
    }
    // Neighbours: only the column facing the new chunk changed neighbourhood.
    static constexpr int kSides[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto &side : kSides) {
        const int nx = cx + side[0];
        const int nz = cz + side[1];
        const voxel::Chunk *chunk = chunks_.find(nx, nz);
        if (chunk == nullptr) {
            continue;
        }
        for_each_fluid_cell(*chunk, [&](int lx, int y, int lz) {
            const bool facing = side[0] != 0 ? lx == (side[0] > 0 ? 0 : voxel::Chunk::kSizeX - 1)
                                             : lz == (side[1] > 0 ? 0 : voxel::Chunk::kSizeZ - 1);
            if (!facing) {
                return;
            }
            fluid_.wake(nx * voxel::Chunk::kSizeX + lx, y, nz * voxel::Chunk::kSizeZ + lz);
        });
    }
}

void WorldSource::fluid_step(std::vector<std::pair<int, int>> &dirty) {
    fluid_.step();
    if (fluid_dirty_.empty()) {
        return;
    }
    std::sort(fluid_dirty_.begin(), fluid_dirty_.end());
    fluid_dirty_.erase(std::unique(fluid_dirty_.begin(), fluid_dirty_.end()), fluid_dirty_.end());
    dirty.insert(dirty.end(), fluid_dirty_.begin(), fluid_dirty_.end());
    fluid_dirty_.clear();
}

bool WorldSource::place_water_source(int wx, int wy, int wz) {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return false;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    if (chunks_.find(cx, cz) == nullptr) {
        return false; // unloaded: the write would be dropped
    }
    fluid_.place_source(wx, wy, wz, voxel::FluidKind::Water);
    return true;
}

bool WorldSource::is_water_source(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return false;
    }
    const voxel::Chunk *chunk = chunks_.find_world(wx, wz);
    if (chunk == nullptr) {
        return false;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const int lx = wx - cx * voxel::Chunk::kSizeX;
    const int lz = wz - cz * voxel::Chunk::kSizeZ;
    const voxel::FluidCell cell = chunk->get_fluid(lx, wy, lz);
    if (!cell.empty()) {
        return cell.source; // flowing water cannot be scooped up, as in MC
    }
    // Worldgen water is not in the fluid layer; treat it as a source so a
    // bucket works on an ocean exactly like it does on a poured source.
    return chunk->get_block(lx, wy, lz) == water_block_id_;
}

bool WorldSource::remove_water_source(int wx, int wy, int wz, std::vector<std::pair<int, int>> &dirty) {
    if (!is_water_source(wx, wy, wz)) {
        return false;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return false;
    }
    const int lx = wx - cx * voxel::Chunk::kSizeX;
    const int lz = wz - cz * voxel::Chunk::kSizeZ;
    if (!chunk->get_fluid(lx, wy, lz).empty()) {
        fluid_.clear_cell(wx, wy, wz); // clears the fluid cell and the placeholder block
        return true;
    }
    set_block(wx, wy, wz, 0, dirty); // legacy static water block
    fluid_.on_block_changed(wx, wy, wz);
    return true;
}

void shade_mesh_with_light(render::MeshData &mesh, const WorldSource &world, int cx, int cz) {
    constexpr float kAmbientFloor = 0.05f;
    constexpr float kDayFactor = 1.0f; // day/night cycle is M2; fixed daylight for T008

    auto shade_bucket = [&](render::MeshBucket &bucket) {
        for (std::size_t quad = 0; quad + 3 < bucket.vertices.size(); quad += 4) {
            const auto &v0 = bucket.vertices[quad];
            const auto &v1 = bucket.vertices[quad + 1];
            const auto &v2 = bucket.vertices[quad + 2];
            const glm::vec3 e1(static_cast<float>(v1.x) - v0.x, static_cast<float>(v1.y) - v0.y,
                               static_cast<float>(v1.z) - v0.z);
            const glm::vec3 e2(static_cast<float>(v2.x) - v0.x, static_cast<float>(v2.y) - v0.y,
                               static_cast<float>(v2.z) - v0.z);
            const glm::vec3 normal = glm::cross(e1, e2);
            // The dominant axis of the cross product is the face normal axis;
            // CCW-from-outside winding makes its sign point out of the block.
            int axis = 0;
            for (int a = 1; a < 3; ++a) {
                if (std::abs(normal[a]) > std::abs(normal[axis])) {
                    axis = a;
                }
            }
            const int sign = normal[axis] > 0.0f ? 1 : -1;

            // Block cell: on the normal axis the face plane sits at the
            // block's outer boundary (plane-1 for a positive normal, plane
            // for a negative one); on the tangent axes it is the min corner.
            const auto comp = [](const render::MeshVertex &v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); };
            const int plane = comp(v0, axis);
            int block_cell[3] = {v0.x, v0.y, v0.z};
            for (int a = 0; a < 3; ++a) {
                if (a == axis) {
                    block_cell[a] = sign > 0 ? plane - 1 : plane;
                } else {
                    // Tangent axis: the min corner over the quad's 4 vertices.
                    block_cell[a] = std::min({comp(bucket.vertices[quad], a), comp(bucket.vertices[quad + 1], a),
                                              comp(bucket.vertices[quad + 2], a), comp(bucket.vertices[quad + 3], a)});
                }
            }
            const int air[3] = {block_cell[0] + (axis == 0 ? sign : 0), block_cell[1] + (axis == 1 ? sign : 0),
                                block_cell[2] + (axis == 2 ? sign : 0)};

            const int wx = cx * render::kChunkSizeX + air[0];
            const int wz = cz * render::kChunkSizeZ + air[2];
            std::uint8_t sky = 15;
            std::uint8_t block_light = 0;
            if (air[1] >= 0 && air[1] < render::kChunkSizeY) {
                const auto levels = world.light().light_at(wx, air[1], wz);
                sky = levels.sky;
                block_light = levels.block;
            }
            const float brightness = std::max(
                kAmbientFloor, std::max(static_cast<float>(sky) * kDayFactor, static_cast<float>(block_light)) / 15.0f);

            for (int c = 0; c < 4; ++c) {
                auto &v = bucket.vertices[quad + static_cast<std::size_t>(c)];
                const int shaded = static_cast<int>(static_cast<float>(v.shade) * brightness + 0.5f);
                v.shade = static_cast<std::uint8_t>(std::clamp(shaded, 0, 255));
            }
        }
    };
    shade_bucket(mesh.opaque);
    shade_bucket(mesh.translucent);
}

} // namespace opencraft::client
