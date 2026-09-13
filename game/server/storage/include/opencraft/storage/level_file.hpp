#pragma once

#include <cstdint>
#include <filesystem>

#include "opencraft/core/byte_buffer.hpp"

namespace opencraft::storage {

// Global world state persisted to <saves>/<world>/level.ocd (T009; docs/02
// §5 calls this level.dat-equivalent). Binary, self-describing, versioned:
//
//   u32 magic "OCLD" (0x4F434C44)
//   u16 format version (= 1)
//   u16 header size (= 12)
//   u32 payload length in bytes
//   payload (ByteBuffer-written, little-endian)
//   u32 CRC-32 of the payload
//
// Payload field order (do not reorder; readers are field-by-field so later
// versions may append):
//   u64 seed
//   u64 game tick count
//   u8  has_player (0/1; 0 = spawn scan should run, the remaining player
//       fields are still written but meaningless)
//   f64 spawn_x, spawn_y, spawn_z
//   f64 player position x, y, z (feet-center, T007 PlayerState)
//   f64 player velocity x, y, z
//   f64 yaw, pitch, health, fall_peak_y, fall_distance
//   u8  pose (physics::Pose), u8 on_ground
//   u16 selected hotbar block id
//
// Doubles travel as raw u64 bit patterns (little-endian) - ByteBuffer only
// gained u16/u64 scalars in this task, so f64 goes through memcpy-bit-cast.
struct LevelData {
    static constexpr std::uint32_t kMagic = 0x4F434C44; // "OCLD"
    static constexpr std::uint16_t kFormatVersion = 1;

    std::uint64_t seed = 0;
    std::uint64_t tick_count = 0;
    bool has_player = false;

    double spawn_x = 0.0;
    double spawn_y = 0.0;
    double spawn_z = 0.0;

    double player_x = 0.0;
    double player_y = 0.0;
    double player_z = 0.0;
    double player_vx = 0.0;
    double player_vy = 0.0;
    double player_vz = 0.0;
    double yaw = 0.0;
    double pitch = 0.0;
    double health = 20.0;
    double fall_peak_y = 0.0;
    double fall_distance = 0.0;
    std::uint8_t pose = 0;
    bool on_ground = true;
    std::uint16_t selected_block = 0;
};

// Throws std::runtime_error on any structural problem (bad magic/version/
// length/CRC, truncated file). Missing file also throws - callers decide
// whether that means "new world" or an error. (No [[nodiscard]]: calling it
// just to validate a file is legitimate.)
LevelData read_level(const std::filesystem::path &path);

// Atomic write: path.tmp -> fsync -> rename.
void write_level(const std::filesystem::path &path, const LevelData &level);

[[nodiscard]] core::ByteBuffer serialize_level(const LevelData &level);
[[nodiscard]] LevelData deserialize_level(core::ByteBuffer &payload);

} // namespace opencraft::storage
