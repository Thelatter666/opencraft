#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "opencraft/core/byte_buffer.hpp"

namespace opencraft::voxel {

// Both light channels of one block: 0-15 each (docs/03 §5). Rendering later
// combines them as max(sky * daylight_factor, block); the data layer only
// stores the two channels independently.
struct LightLevels {
    std::uint8_t sky = 0;
    std::uint8_t block = 0;
};

// Per-chunk light data, kept alongside (not inside) Chunk so the block
// storage format v1 stays untouched (T006 constraint). One byte per block:
// high nibble skylight, low nibble blocklight. Section-internal index order
// matches Chunk: YZX, index = (y * 16 + z) * 16 + x.
class LightStorage {
public:
    static constexpr int kSizeX = 16;
    static constexpr int kSizeY = 384;
    static constexpr int kSizeZ = 16;
    static constexpr std::size_t kVolume = static_cast<std::size_t>(kSizeX) * kSizeY * kSizeZ; // 98304

    static constexpr std::uint32_t kFormatVersion = 1;

    LightStorage() = default;
    LightStorage(int chunk_x, int chunk_z);

    [[nodiscard]] int chunk_x() const { return chunk_x_; }

    [[nodiscard]] int chunk_z() const { return chunk_z_; }

    // --- local-coordinate access (0..15 / 0..383) -----------------------------
    // Throws std::out_of_range when out of bounds; levels are masked to 0-15.
    [[nodiscard]] std::uint8_t sky(int x, int y, int z) const;
    [[nodiscard]] std::uint8_t block_light(int x, int y, int z) const;
    void set_sky(int x, int y, int z, std::uint8_t level);
    void set_block_light(int x, int y, int z, std::uint8_t level);

    // --- world-coordinate access (relative to this storage's chunk origin) ----
    [[nodiscard]] std::uint8_t sky_world(int world_x, int world_y, int world_z) const;
    [[nodiscard]] std::uint8_t block_light_world(int world_x, int world_y, int world_z) const;
    void set_sky_world(int world_x, int world_y, int world_z, std::uint8_t level);
    void set_block_light_world(int world_x, int world_y, int world_z, std::uint8_t level);

    [[nodiscard]] LightLevels levels_world(int world_x, int world_y, int world_z) const;

    void clear();

    // Format v1: u32 version prefix, then kVolume raw bytes (high nibble sky,
    // low nibble block, YZX order). Chunk coordinates are not stored on the
    // wire; the caller keys storages by chunk coordinate.
    void serialize(core::ByteBuffer &out) const;
    [[nodiscard]] static LightStorage deserialize(core::ByteBuffer &in);

    // YZX order, same layout contract as Chunk's section indexing.
    [[nodiscard]] static std::size_t local_index(int x, int y, int z);

private:
    static void validate_xyz(int x, int y, int z);
    // Local world coordinates are already reduced to 0..15 by the caller.
    [[nodiscard]] std::uint8_t nibble(std::size_t index, bool high) const;
    void set_nibble(std::size_t index, bool high, std::uint8_t level);

    int chunk_x_ = 0;
    int chunk_z_ = 0;
    std::vector<std::uint8_t> data_ = std::vector<std::uint8_t>(kVolume, 0);
};

} // namespace opencraft::voxel
