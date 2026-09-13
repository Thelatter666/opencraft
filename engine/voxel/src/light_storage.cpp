#include "opencraft/voxel/light_storage.hpp"

#include <stdexcept>
#include <string>

namespace opencraft::voxel {

namespace {

// Same local LE helpers convention as chunk.cpp (u32 is the only width the
// core ByteBuffer exposes as a scalar beyond u8/bytes).
void put_u32(core::ByteBuffer &out, std::uint32_t value) {
    const std::uint8_t bytes[4] = {
        static_cast<std::uint8_t>(value & 0xFF), static_cast<std::uint8_t>((value >> 8) & 0xFF),
        static_cast<std::uint8_t>((value >> 16) & 0xFF), static_cast<std::uint8_t>((value >> 24) & 0xFF)};
    out.write_bytes(bytes, 4);
}

[[nodiscard]] std::uint32_t get_u32(core::ByteBuffer &in) {
    std::uint8_t bytes[4] = {};
    in.read_bytes(bytes, 4);
    return static_cast<std::uint32_t>(bytes[0]) | static_cast<std::uint32_t>(bytes[1]) << 8 |
           static_cast<std::uint32_t>(bytes[2]) << 16 | static_cast<std::uint32_t>(bytes[3]) << 24;
}

} // namespace

LightStorage::LightStorage(int chunk_x, int chunk_z) : chunk_x_(chunk_x), chunk_z_(chunk_z) {
}

void LightStorage::validate_xyz(int x, int y, int z) {
    if (x < 0 || x >= kSizeX || y < 0 || y >= kSizeY || z < 0 || z >= kSizeZ) {
        throw std::out_of_range("light coordinates out of chunk bounds: " + std::to_string(x) + "," +
                                std::to_string(y) + "," + std::to_string(z));
    }
}

std::size_t LightStorage::local_index(int x, int y, int z) {
    return (static_cast<std::size_t>(y) * kSizeZ + static_cast<std::size_t>(z)) * kSizeX + static_cast<std::size_t>(x);
}

std::uint8_t LightStorage::nibble(std::size_t index, bool high) const {
    const std::uint8_t byte = data_[index];
    return high ? static_cast<std::uint8_t>(byte >> 4) : static_cast<std::uint8_t>(byte & 0x0F);
}

void LightStorage::set_nibble(std::size_t index, bool high, std::uint8_t level) {
    const std::uint8_t masked = static_cast<std::uint8_t>(level & 0x0F);
    auto &byte = data_[index];
    if (high) {
        byte = static_cast<std::uint8_t>((byte & 0x0F) | (masked << 4));
    } else {
        byte = static_cast<std::uint8_t>((byte & 0xF0) | masked);
    }
}

std::uint8_t LightStorage::sky(int x, int y, int z) const {
    validate_xyz(x, y, z);
    return nibble(local_index(x, y, z), true);
}

std::uint8_t LightStorage::block_light(int x, int y, int z) const {
    validate_xyz(x, y, z);
    return nibble(local_index(x, y, z), false);
}

void LightStorage::set_sky(int x, int y, int z, std::uint8_t level) {
    validate_xyz(x, y, z);
    set_nibble(local_index(x, y, z), true, level);
}

void LightStorage::set_block_light(int x, int y, int z, std::uint8_t level) {
    validate_xyz(x, y, z);
    set_nibble(local_index(x, y, z), false, level);
}

// World forms reduce to local by subtracting this storage's chunk origin;
// floor behavior matches Chunk::chunk_coords (16-aligned origins).
std::uint8_t LightStorage::sky_world(int world_x, int world_y, int world_z) const {
    return sky(world_x - chunk_x_ * kSizeX, world_y, world_z - chunk_z_ * kSizeZ);
}

std::uint8_t LightStorage::block_light_world(int world_x, int world_y, int world_z) const {
    return block_light(world_x - chunk_x_ * kSizeX, world_y, world_z - chunk_z_ * kSizeZ);
}

void LightStorage::set_sky_world(int world_x, int world_y, int world_z, std::uint8_t level) {
    set_sky(world_x - chunk_x_ * kSizeX, world_y, world_z - chunk_z_ * kSizeZ, level);
}

void LightStorage::set_block_light_world(int world_x, int world_y, int world_z, std::uint8_t level) {
    set_block_light(world_x - chunk_x_ * kSizeX, world_y, world_z - chunk_z_ * kSizeZ, level);
}

LightLevels LightStorage::levels_world(int world_x, int world_y, int world_z) const {
    return LightLevels{sky_world(world_x, world_y, world_z), block_light_world(world_x, world_y, world_z)};
}

void LightStorage::clear() {
    std::fill(data_.begin(), data_.end(), std::uint8_t{0});
}

void LightStorage::serialize(core::ByteBuffer &out) const {
    put_u32(out, kFormatVersion);
    out.write_bytes(data_.data(), data_.size());
}

LightStorage LightStorage::deserialize(core::ByteBuffer &in) {
    const std::uint32_t version = get_u32(in);
    if (version != kFormatVersion) {
        throw std::runtime_error("unsupported LightStorage format version: " + std::to_string(version));
    }
    LightStorage storage;
    in.read_bytes(storage.data_.data(), storage.data_.size());
    return storage;
}

} // namespace opencraft::voxel
