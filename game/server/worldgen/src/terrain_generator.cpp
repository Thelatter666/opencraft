#include "opencraft/worldgen/terrain_generator.hpp"

#include <array>
#include <cmath>

namespace opencraft::worldgen {

namespace {

// Per-subsystem seed streams: each route is the world seed XORed with a fixed
// constant (docs/research/02 §1.5: 子系统各自独立种子流，避免"改一处全盘变动").
// The constants are arbitrary but frozen; changing any of them changes every
// golden hash.
constexpr std::uint64_t kTerrainSalt = 0x1A2B3C4D5E6F7081ULL;
constexpr std::uint64_t kCaveSalt = 0x2B3C4D5E6F708192ULL;
// Salt for the per-block bedrock position hash.
constexpr std::uint64_t kBedrockSalt = 0xBEDB0CA5EED00001ULL;

// Instance salts inside a route: noise::mix64(route_base ^ salt) gives each noise
// channel an independent 64-bit seed.
constexpr std::uint64_t kSaltContinent = 0x1000ULL;
constexpr std::uint64_t kSaltMountains = 0x2000ULL;
constexpr std::uint64_t kSaltDetail = 0x3000ULL;
constexpr std::uint64_t kSaltFloorMaterial = 0x4000ULL;
constexpr std::uint64_t kSaltCheese = 0x5000ULL;
constexpr std::uint64_t kSaltSpaghettiA = 0x6000ULL;
constexpr std::uint64_t kSaltSpaghettiB = 0x7000ULL;

// --- terrain shaping (all parameters ours and frozen; see task card) ---

// Height field: h = 67 + 26 * continent, so oceans dip to ~41 and inland
// plateaus rise to ~93. Mountains are rare on purpose: the ridged channel
// peaks where m is strongly negative (ridged noise is mostly positive, so we
// flip it), and the bonus only kicks in inland (continent > gate), scaled up
// over the gate span — coastal and spawn land stay plains, far ranges reach
// ~Y=150 and carry snow caps. Sea level is 63.
constexpr float kHeightBase = 67.0f;
constexpr float kHeightAmp = 26.0f;
constexpr float kMountainStart = 0.72f;    // ridge strength where peaks begin
constexpr float kMountainAmp = 260.0f;     // max bonus ~ (1-0.72) * 260 ≈ 45
constexpr float kMountainGate = 0.25f;     // continent needed for mountains
constexpr float kMountainGateSpan = 0.35f; // ramp from gate to full strength

// Density = height bias + 3D detail (docs/research/02 §2.3: 密度函数 + 高度
// 偏置). The bias term squashes the field vertically toward the target
// surface; the detail term adds overhangs within roughly +-12 blocks of it.
constexpr float kBiasScale = 28.0f; // bias = (h - y) / kBiasScale
constexpr float kDetailAmp = 0.42f;
// Outside this |bias| the detail term cannot flip the sign, so its noise is
// skipped entirely (pure optimization, bit-identical results).
constexpr float kDetailCutoff = 1.5f;

// --- caves (two simplified shapes; 入口可达地表不强制) ---

// Cheese caves: carve where the noise exceeds a threshold that drops with
// depth, so big halls live mostly below Y=-10. Thresholds 0.63/0.57 keep
// caverns sparse but roomy.
constexpr float kCheeseThresholdHigh = 0.63f; // above the depth split
constexpr float kCheeseThresholdLow = 0.57f;  // below the depth split
constexpr float kCheeseDepthSplit = -10.0f;

// Spaghetti caves: two independent noise channels; a tunnel exists where both
// channels cross zero at once (|a| < w && |b| < w), which yields thin,
// winding tubes. Width 0.08 gives roughly 2-4 block wide tunnels.
constexpr float kSpaghettiWidth = 0.08f;

// Ocean-floor guard: caves never come closer than this to the floor of a
// water column, so no cave opens directly under the sea (no floating water).
constexpr float kOceanFloorMargin = 4.0f;

// --- surface rules ---

// Land: grass/dirt below kStoneTopY, bare stone from there up, snow caps from
// kSnowTopY up (chosen reachable: the height field peaks near ~Y=123).
// Underwater columns get sand or gravel on the floor over dirt.
constexpr int kStoneTopY = 96;
constexpr int kSnowTopY = 112;
constexpr int kDirtDepth = 3;      // dirt blocks under the land surface block
constexpr int kFloorDirtDepth = 2; // dirt blocks under the sea floor material

// 3D-detail pockets this close below the nominal surface are sealed with
// stone instead of left as dry air: otherwise the bottom water cell of an
// ocean column could rest on an air pocket (floating water). Deeper pockets
// stay air — they are enclosed caves.
constexpr float kPocketSealDepth = 6.0f;

// --- bedrock gradient (docs/research/02 §2.4: Y=-64 全基岩 -> Y=-59 消失) ---
constexpr int kBedrockTopY = -59; // highest Y that can carry bedrock
// Per-Y bedrock probability for Y in [-63, -59]; Y=-64 is always bedrock.
constexpr float kBedrockChance[] = {0.75f, 0.5f, 0.3f, 0.15f, 0.05f};

} // namespace

TerrainGenerator::TerrainGenerator(std::uint64_t seed, const voxel::BlockRegistry &registry)
    : seed_(seed), ids_{registry.id_of("stone"),  registry.id_of("dirt"),       registry.id_of("grass_block"),
                        registry.id_of("water"),  registry.id_of("bedrock"),    registry.id_of("sand"),
                        registry.id_of("gravel"), registry.id_of("snow_block"), registry.air()},
      continent_(noise::mix64(seed ^ kTerrainSalt) ^ kSaltContinent,
                 {/*frequency*/ 1.0f / 340.0f, /*octaves*/ 4, 2.0f, 0.5f, false}),
      mountains_(noise::mix64(seed ^ kTerrainSalt) ^ kSaltMountains,
                 {/*frequency*/ 1.0f / 300.0f, /*octaves*/ 3, 2.0f, 0.5f, true}),
      detail_(noise::mix64(seed ^ kTerrainSalt) ^ kSaltDetail,
              {/*frequency*/ 1.0f / 60.0f, /*octaves*/ 3, 2.0f, 0.5f, false}),
      floor_material_(noise::mix64(seed ^ kTerrainSalt) ^ kSaltFloorMaterial,
                      {/*frequency*/ 1.0f / 48.0f, /*octaves*/ 2, 2.0f, 0.5f, false}),
      cheese_(noise::mix64(seed ^ kCaveSalt) ^ kSaltCheese,
              {/*frequency*/ 1.0f / 100.0f, /*octaves*/ 2, 2.0f, 0.5f, false}),
      spaghetti_a_(noise::mix64(seed ^ kCaveSalt) ^ kSaltSpaghettiA,
                   {/*frequency*/ 1.0f / 90.0f, /*octaves*/ 2, 2.0f, 0.5f, false}),
      spaghetti_b_(noise::mix64(seed ^ kCaveSalt) ^ kSaltSpaghettiB,
                   {/*frequency*/ 1.0f / 90.0f, /*octaves*/ 2, 2.0f, 0.5f, false}) {
}

float TerrainGenerator::column_height(float wx, float wz) const {
    const float c = continent_.sample2(wx, wz);
    float h = kHeightBase + kHeightAmp * c;
    const float m = -mountains_.sample2(wx, wz); // ridged channel, flipped
    const float inland = std::fmin(1.0f, (c - kMountainGate) / kMountainGateSpan);
    if (m > kMountainStart && inland > 0.0f) {
        h += (m - kMountainStart) * kMountainAmp * inland;
    }
    return h;
}

bool TerrainGenerator::carve(float wx, float wy, float wz) const {
    const float threshold = wy < kCheeseDepthSplit ? kCheeseThresholdLow : kCheeseThresholdHigh;
    if (cheese_.sample3(wx, wy, wz) > threshold) {
        return true;
    }
    const float a = spaghetti_a_.sample3(wx, wy, wz);
    if (a >= kSpaghettiWidth || a <= -kSpaghettiWidth) {
        return false; // early out: most cells fail channel 1
    }
    const float b = spaghetti_b_.sample3(wx, wy, wz);
    return b < kSpaghettiWidth && b > -kSpaghettiWidth;
}

std::uint64_t TerrainGenerator::position_hash(std::int64_t wx, int wy, std::int64_t wz, std::uint64_t salt) const {
    std::uint64_t h = noise::mix64(seed_ ^ salt);
    h = noise::mix64(h ^ (static_cast<std::uint64_t>(wx) * 0x9E3779B97F4A7C15ULL));
    h = noise::mix64(h ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(wy)) * 0xC2B2AE3D27D4EB4FULL));
    h = noise::mix64(h ^ (static_cast<std::uint64_t>(wz) * 0x165667B19E3779F9ULL));
    return h;
}

void TerrainGenerator::generate_chunk(std::int64_t cx, std::int64_t cz, voxel::Chunk &out) const {
    constexpr int kSizeX = voxel::Chunk::kSizeX;
    constexpr int kSizeZ = voxel::Chunk::kSizeZ;
    constexpr int kSizeY = voxel::Chunk::kSizeY;

    // Per-column pre-pass: nominal surface height (pre-cave). Water and the
    // surface rules key off this; the density field keys off it per cell.
    // (float coordinates: world spans stay well inside float's 2^24 exact
    // integer range for any realistic testing/travel radius.)
    std::array<float, static_cast<std::size_t>(kSizeX) * kSizeZ> heights{};
    std::array<bool, static_cast<std::size_t>(kSizeX) * kSizeZ> sandy{};
    for (int lz = 0; lz < kSizeZ; ++lz) {
        for (int lx = 0; lx < kSizeX; ++lx) {
            const auto wx = static_cast<float>(cx * kSizeX + lx);
            const auto wz = static_cast<float>(cz * kSizeZ + lz);
            heights[static_cast<std::size_t>(lz) * kSizeX + lx] = column_height(wx, wz);
            sandy[static_cast<std::size_t>(lz) * kSizeX + lx] = floor_material_.sample2(wx, wz) > 0.0f;
        }
    }

    // Pass 1: density terrain + caves + water.
    for (int lz = 0; lz < kSizeZ; ++lz) {
        for (int lx = 0; lx < kSizeX; ++lx) {
            const std::size_t col = static_cast<std::size_t>(lz) * kSizeX + lx;
            const float h = heights[col];
            const bool ocean_column = h <= static_cast<float>(kSeaLevel);
            const auto wx = static_cast<float>(cx * kSizeX + lx);
            const auto wz = static_cast<float>(cz * kSizeZ + lz);

            for (int ly = 0; ly < kSizeY; ++ly) {
                const int wy = ly + kMinWorldY; // local 0 == world Y -64
                const auto fy = static_cast<float>(wy);
                const float bias = (h - fy) / kBiasScale;

                bool solid;
                if (bias >= kDetailCutoff) {
                    solid = true; // deep underground: detail cannot flip it
                } else if (bias > -kDetailCutoff) {
                    solid = bias + detail_.sample3(wx, fy, wz) * kDetailAmp > 0.0f;
                } else {
                    solid = false; // high air: detail cannot flip it
                }

                if (solid) {
                    // Caves: on land any solid cell can be carved (entrances
                    // may reach the surface); under water columns keep the
                    // margin so a cave never opens beneath the sea floor.
                    const bool carved = (!ocean_column || fy < h - kOceanFloorMargin) && carve(wx, fy, wz);
                    if (!carved) {
                        out.set_block(lx, ly, lz, ids_.stone);
                    } else {
                        out.set_block(lx, ly, lz, ids_.air);
                    }
                } else if (wy <= kSeaLevel && fy > h) {
                    // Open air below sea level but above the nominal surface:
                    // open water. Air pockets below the surface (3D-noise
                    // overhang pockets, caves) stay dry so water never hangs
                    // inside a cavity.
                    out.set_block(lx, ly, lz, ids_.water);
                } else if (fy <= h && fy > h - kPocketSealDepth) {
                    // Near-surface pocket: seal it so no water (or open sky
                    // crater) can rest on dry air right under the surface.
                    out.set_block(lx, ly, lz, ids_.stone);
                } else {
                    out.set_block(lx, ly, lz, ids_.air);
                }
            }
        }
    }

    // Pass 2: surface rule layer (docs/research/02 §2.4). All terrain cells
    // are stone at this point, so the topmost non-air/non-water cell per
    // column is the surface to dress.
    for (int lz = 0; lz < kSizeZ; ++lz) {
        for (int lx = 0; lx < kSizeX; ++lx) {
            const std::size_t col = static_cast<std::size_t>(lz) * kSizeX + lx;
            const float h = heights[col];
            int surface_ly = -1;
            for (int ly = kSizeY - 1; ly >= 0; --ly) {
                const std::uint16_t id = out.get_block(lx, ly, lz);
                if (id != ids_.water && id != ids_.air) {
                    surface_ly = ly;
                    break;
                }
            }
            if (surface_ly < 0) {
                continue; // fully air column (cannot happen, but stay safe)
            }
            const int surface_y = surface_ly + kMinWorldY;

            if (h <= static_cast<float>(kSeaLevel)) {
                // Sea floor: sand or gravel on top, dirt underneath.
                const std::uint16_t floor_id = sandy[col] ? ids_.sand : ids_.gravel;
                if (out.get_block(lx, surface_ly, lz) == ids_.stone) {
                    out.set_block(lx, surface_ly, lz, floor_id);
                }
                for (int d = 1; d <= kFloorDirtDepth && surface_ly - d >= 0; ++d) {
                    if (out.get_block(lx, surface_ly - d, lz) == ids_.stone) {
                        out.set_block(lx, surface_ly - d, lz, ids_.dirt);
                    }
                }
            } else if (surface_y >= kSnowTopY) {
                if (out.get_block(lx, surface_ly, lz) == ids_.stone) {
                    out.set_block(lx, surface_ly, lz, ids_.snow);
                }
            } else if (surface_y < kStoneTopY) {
                // Land: grass on top, dirt under it.
                if (out.get_block(lx, surface_ly, lz) == ids_.stone) {
                    out.set_block(lx, surface_ly, lz, ids_.grass);
                }
                for (int d = 1; d <= kDirtDepth && surface_ly - d >= 0; ++d) {
                    if (out.get_block(lx, surface_ly - d, lz) == ids_.stone) {
                        out.set_block(lx, surface_ly - d, lz, ids_.dirt);
                    }
                }
            }
            // kStoneTopY..kSnowTopY: bare stone peaks, nothing to replace.
        }
    }

    // Pass 3: bedrock gradient, overriding everything below (caves included —
    // the world floor stays sealed).
    for (int lz = 0; lz < kSizeZ; ++lz) {
        for (int lx = 0; lx < kSizeX; ++lx) {
            const auto wx = cx * kSizeX + lx;
            const auto wz = cz * kSizeZ + lz;
            out.set_block(lx, 0, lz, ids_.bedrock); // Y=-64: solid bedrock
            for (int wy = kMinWorldY + 1; wy <= kBedrockTopY; ++wy) {
                const float chance = kBedrockChance[wy - (kMinWorldY + 1)];
                const auto roll =
                    static_cast<float>(position_hash(wx, wy, wz, kBedrockSalt) >> 40) / static_cast<float>(1ULL << 24);
                if (roll < chance) {
                    out.set_block(lx, wy - kMinWorldY, lz, ids_.bedrock);
                }
            }
        }
    }
}

} // namespace opencraft::worldgen
