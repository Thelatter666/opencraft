#include "terrain.hpp"

#include "opencraft/core/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace opencraft::client {

namespace {

std::uint32_t mix_hash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Deterministic per-column ground height: layered sines stand in for T004's
// noise-based generator.
int ground_height_impl(int wx, int wz) {
    const float h = 11.5f + 3.5f * std::sin(wx * 0.09f) + 3.0f * std::cos(wz * 0.08f) +
                    1.5f * std::sin((wx + wz) * 0.15f) + 1.0f * std::cos(wx * 0.21f - wz * 0.17f);
    return std::clamp(static_cast<int>(h), 1, 24);
}

// Deterministic hash of a block position, used for ore specks.
std::uint32_t block_hash(int wx, int wy, int wz) {
    return mix_hash(static_cast<std::uint32_t>(wx) * 73856093U + static_cast<std::uint32_t>(wy) * 19349663U +
                    static_cast<std::uint32_t>(wz) * 83492791U);
}

} // namespace

TestTerrain::TestTerrain() {
    registry_ = voxel::BlockRegistry::create_default();
    for (int cx = -kRadius; cx <= kRadius; ++cx) {
        for (int cz = -kRadius; cz <= kRadius; ++cz) {
            static_cast<void>(chunks_.get_or_load(cx, cz));
        }
    }
    for (int wx = -kRadius * voxel::Chunk::kSizeX; wx < kRadius * voxel::Chunk::kSizeX + voxel::Chunk::kSizeX; ++wx) {
        for (int wz = -kRadius * voxel::Chunk::kSizeZ; wz < kRadius * voxel::Chunk::kSizeZ + voxel::Chunk::kSizeZ;
             ++wz) {
            build_column(wx, wz);
        }
    }

    // Carve a 3x3 pit on a dry spot near the world center so the camera can
    // look into it (stone walls, bedrock floor). Scans outward until a fully
    // dry 3x3 area is found; deterministic given the height function.
    int pit_x = 0;
    int pit_z = 0;
    bool pit_found = false;
    for (int r = 0; r < 40 && !pit_found; ++r) {
        for (int d = -r; d <= r && !pit_found; ++d) {
            for (const auto [wx, wz] : {std::pair{d, -r}, {d, r}, {-r, d}, {r, d}}) {
                bool dry = true;
                for (int dx = -1; dx <= 1 && dry; ++dx) {
                    for (int dz = -1; dz <= 1 && dry; ++dz) {
                        if (ground_height_impl(wx + dx, wz + dz) < kWaterLevel + 2) {
                            dry = false;
                        }
                    }
                }
                if (dry) {
                    pit_x = wx;
                    pit_z = wz;
                    pit_found = true;
                }
            }
        }
    }
    if (pit_found) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                const int h = ground_height_impl(pit_x + dx, pit_z + dz);
                for (int y = 1; y <= h; ++y) {
                    set_world_block(pit_x + dx, y, pit_z + dz, registry_.air());
                }
            }
        }
    }

    // Two trees on dry land near the center.
    int trees_planted = 0;
    for (int r = 3; r < 40 && trees_planted < 2; r += 2) {
        for (int d = -r; d <= r && trees_planted < 2; d += 3) {
            for (const auto [wx, wz] : {std::pair{d, -r}, {d, r}, {-r, d}, {r, d}}) {
                const int h = ground_height_impl(wx, wz);
                if (h < kWaterLevel + 2) {
                    continue;
                }
                if (std::abs(wx - pit_x) <= 5 && std::abs(wz - pit_z) <= 5) {
                    continue; // keep the pit unobstructed
                }
                const std::uint16_t log_id = registry_.id_of("log");
                const std::uint16_t leaves_id = registry_.id_of("leaves");
                for (int y = h + 1; y <= h + 4; ++y) {
                    set_world_block(wx, y, wz, log_id);
                }
                for (int ty = h + 3; ty <= h + 5; ++ty) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dz = -1; dz <= 1; ++dz) {
                            if (dx == 0 && dz == 0 && ty <= h + 4) {
                                continue; // trunk occupies the center
                            }
                            set_world_block(wx + dx, ty, wz + dz, leaves_id);
                        }
                    }
                }
                set_world_block(wx, h + 6, wz, leaves_id);
                ++trees_planted;
                break;
            }
        }
    }
}

int TestTerrain::ground_height(int wx, int wz) const {
    return ground_height_impl(wx, wz);
}

void TestTerrain::build_column(int wx, int wz) {
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    voxel::Chunk &chunk = chunks_.get_or_load(cx, cz);
    const int lx = wx - cx * voxel::Chunk::kSizeX;
    const int lz = wz - cz * voxel::Chunk::kSizeZ;
    const int h = ground_height_impl(wx, wz);

    const std::uint16_t bedrock = registry_.id_of("bedrock");
    const std::uint16_t stone = registry_.id_of("stone");
    const std::uint16_t dirt = registry_.id_of("dirt");
    const std::uint16_t grass = registry_.id_of("grass_block");
    const std::uint16_t sand = registry_.id_of("sand");
    const std::uint16_t water = registry_.id_of("water");
    const std::uint16_t snow = registry_.id_of("snow_block");
    const std::uint16_t coal = registry_.id_of("coal_ore");
    const std::uint16_t iron = registry_.id_of("iron_ore");

    auto set = [&](int y, std::uint16_t id) { chunk.set_block(lx, y, lz, id); };
    set(0, bedrock);
    for (int y = 1; y <= h; ++y) {
        if (y <= h - 3) {
            // Sparse ore specks in the stone body, fully deterministic.
            const std::uint32_t hash = block_hash(wx, y, wz);
            if (hash % 100U < 3U) {
                set(y, coal);
            } else if (hash % 100U < 5U) {
                set(y, iron);
            } else {
                set(y, stone);
            }
        } else if (y < h) {
            set(y, dirt);
        } else { // y == h: surface block
            if (h < kWaterLevel) {
                set(y, sand); // submerged / shore columns
            } else if (h >= 18) {
                set(y, snow); // peaks
            } else {
                set(y, grass);
            }
        }
    }
    for (int y = h + 1; y <= kWaterLevel; ++y) {
        set(y, water);
    }
}

// Replaces a block at world coordinates if the owning chunk is loaded.
void TestTerrain::set_world_block(int wx, int wy, int wz, std::uint16_t id) {
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return; // outside the 5x5 demo area
    }
    chunk->set_block(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ, id);
}

std::uint16_t ChunkSource::block_at(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return 0; // outside the world: air (documented IBlockSource contract)
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const std::int64_t key = voxel::Chunk::chunk_coord(wx, wz);
    if (key != cached_key_ || cached_chunk_ == nullptr) {
        cached_chunk_ = chunks_.find(cx, cz);
        cached_key_ = key;
        if (cached_chunk_ == nullptr) {
            return 0; // unloaded chunk: air
        }
    }
    return cached_chunk_->get_block(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ);
}

} // namespace opencraft::client
