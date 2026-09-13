#include "world.hpp"

#include "opencraft/core/log.hpp"
#include "opencraft/worldgen/terrain_generator.hpp"

#include <algorithm>
#include <cmath>

namespace opencraft::client {

WorldSource::WorldSource()
    : registry_(voxel::BlockRegistry::create_default()), light_world_(chunks_, registry_, {}), light_(light_world_),
      generator_(kSeed, registry_) {
}

bool WorldSource::ensure_chunk(int cx, int cz) {
    voxel::Chunk &chunk = chunks_.get_or_load(cx, cz);
    // Detect a fresh chunk: get_or_load hands out an empty one when absent.
    // (A generated chunk is never empty - bedrock floor is always present.)
    if (!chunk.empty()) {
        return false;
    }
    generator_.generate_chunk(cx, cz, chunk);
    light_.init_chunk(cx, cz);
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
