#pragma once

#include <cstdint>

#include "opencraft/noise/noise_sampler.hpp"
#include "opencraft/voxel/block_registry.hpp"
#include "opencraft/voxel/chunk.hpp"

namespace opencraft::worldgen {

// Pure-function overworld terrain generator for T004 (docs/02 §1–§2,
// docs/research/02 §2.3–§2.5):
//   * 3D density field + height bias (OpenSimplex2 fBm) -> solid / air;
//   * sea level Y=63 water fill;
//   * cheese + spaghetti caves (simplified two-shape version; parameters are
//     ours, documented at the use sites in terrain_generator.cpp);
//   * surface rule layer: grass/dirt over stone on land, sand/gravel on the
//     sea floor, bare stone + snow caps on high ground;
//   * bedrock gradient: Y=-64 solid bedrock fading out by Y=-59.
//
// generate_chunk() is a pure function of (seed, cx, cz): it reads no global
// state, keeps no mutable caches and uses no RNG objects — every "random"
// decision is derived by hashing (seed, position) with noise::mix64. It is
// const and safe to call concurrently on the same generator; repeated calls
// fill the chunk byte-identically. Only FastNoiseLite's deterministic
// sampling APIs are used, so the same seed reproduces the same world.
//
// Trees/ores/structures are out of scope (M3); streaming integration into
// ChunkManager belongs to T009.
class TerrainGenerator {
public:
    static constexpr int kMinWorldY = -64;
    static constexpr int kMaxWorldY = 319;
    static constexpr int kSeaLevel = 63;

    // Resolves the block ids used by the generator from `registry` (throws
    // std::out_of_range from the registry if a required block is missing —
    // BlockRegistry::create_default() carries all of them).
    TerrainGenerator(std::uint64_t seed, const voxel::BlockRegistry &registry);

    // Fills `out` completely for chunk (cx, cz); any previous content of the
    // chunk is overwritten. Thread-safe; may be called concurrently.
    void generate_chunk(std::int64_t cx, std::int64_t cz, voxel::Chunk &out) const;

private:
    struct BlockIds {
        std::uint16_t stone;
        std::uint16_t dirt;
        std::uint16_t grass;
        std::uint16_t water;
        std::uint16_t bedrock;
        std::uint16_t sand;
        std::uint16_t gravel;
        std::uint16_t snow;
        std::uint16_t air;
    };

    [[nodiscard]] float column_height(float wx, float wz) const;
    // True when the 3D cave noises carve this cell away. Only ever asked for
    // cells that are currently solid.
    [[nodiscard]] bool carve(float wx, float wy, float wz) const;
    // Position hash for per-block randomness (bedrock gradient).
    [[nodiscard]] std::uint64_t position_hash(std::int64_t wx, int wy, std::int64_t wz, std::uint64_t salt) const;

    std::uint64_t seed_;
    BlockIds ids_;

    // Terrain route (world seed + kTerrainSalt; per-instance seeds derived
    // with mix64 of the route base — see terrain_generator.cpp constants).
    noise::NoiseSampler continent_;      // 2D fBm: broad elevation
    noise::NoiseSampler mountains_;      // 2D ridged: occasional peaks
    noise::NoiseSampler detail_;         // 3D fBm: overhangs near the surface
    noise::NoiseSampler floor_material_; // 2D fBm: sea floor sand vs gravel

    // Cave route (world seed + kCaveSalt).
    noise::NoiseSampler cheese_;      // 3D fBm: big caverns
    noise::NoiseSampler spaghetti_a_; // 3D fBm: winding tunnels, channel 1
    noise::NoiseSampler spaghetti_b_; // 3D fBm: winding tunnels, channel 2
};

} // namespace opencraft::worldgen
