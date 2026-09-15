#pragma once

#include <cstdint>

namespace opencraft::voxel {

// ── fluid layer (T-F1, docs/research/10 §1 / §8.2) ──────────────────────────
//
// The parallel per-voxel fluid channel that sits next to Chunk's block
// palette sections, mirroring the 1.13 "block state + fluid state" split
// (docs/research/10 §1.1). A voxel that holds fluid also holds the fluid
// placeholder block in the block layer, so targeting, mining, physics and
// persistence keep working unchanged.

enum class FluidKind : std::uint8_t {
    None = 0,
    Water = 1,
    Lava = 2,     // reserved: stage 3 of docs/research/10 §8.5
    Reserved = 3, // not assignable
};

// Direction bits of FluidCell::spread. Offsets follow docs/research/10 §4.6
// (+x = East, -x = West, +z = South, -z = North: the doc's example sketch
// grows z downward).
inline constexpr std::uint8_t kFluidEast = 1U << 0;
inline constexpr std::uint8_t kFluidWest = 1U << 1;
inline constexpr std::uint8_t kFluidSouth = 1U << 2;
inline constexpr std::uint8_t kFluidNorth = 1U << 3;
inline constexpr std::uint8_t kFluidAllDirections = kFluidEast | kFluidWest | kFluidSouth | kFluidNorth;

inline constexpr int kFluidMaxLevel = 8;

// One fluid voxel. Five fields instead of docs/research/10 §1.2's three:
//
//   * `source` is NOT derivable from "no fluid above". A falling column can
//     land on top of an existing source, and the cell below must survive the
//     column receding - otherwise the source evaporates with the column.
//   * `spread` is the emit set the slope search produced (§4.5). §3.2 defines
//     the receiver's neighbourhood N4* as "neighbours that can effectively
//     flow toward P", so a cell's new level depends on which neighbours emit
//     toward it, and §4.5 allows any subset of the four directions. Neither
//     fits the single byte suggested by §8.2; see the T-F1 report (D-1).
//     `falling` is a convenience copy of "same fluid above and an open below"
//     (§1.2); a falling cell never emits horizontally (§3.3).
struct FluidCell {
    FluidKind kind = FluidKind::None;
    std::uint8_t level = 0; // L of §0.3: 1..8, 0 = no fluid
    bool falling = false;
    bool source = false;
    std::uint8_t spread = 0; // kFluid* direction bits

    [[nodiscard]] bool empty() const { return kind == FluidKind::None || level == 0; }

    friend bool operator==(const FluidCell &, const FluidCell &) = default;
};

// Packed storage/wire form, 16 bits:
//   bit 15-14 kind   bit 13-10 level   bit 9 falling   bit 8 source
//   bit  7- 4 spare  bit  3- 0 spread
[[nodiscard]] constexpr std::uint16_t pack_fluid(FluidCell cell) {
    const auto kind = static_cast<std::uint16_t>(cell.kind) & 0x3U;
    const auto level = static_cast<std::uint16_t>(cell.level) & 0xFU;
    return static_cast<std::uint16_t>((kind << 14) | (level << 10) | (cell.falling ? 0x200U : 0U) |
                                      (cell.source ? 0x100U : 0U) | (cell.spread & 0xFU));
}

[[nodiscard]] constexpr FluidCell unpack_fluid(std::uint16_t packed) {
    FluidCell cell;
    cell.kind = static_cast<FluidKind>((packed >> 14) & 0x3U);
    cell.level = static_cast<std::uint8_t>((packed >> 10) & 0xFU);
    cell.falling = (packed & 0x200U) != 0;
    cell.source = (packed & 0x100U) != 0;
    cell.spread = static_cast<std::uint8_t>(packed & 0xFU);
    if (cell.kind == FluidKind::None || cell.level == 0) {
        return FluidCell{}; // canonical empty cell
    }
    return cell;
}

// Rejects a packed cell that could only come from a corrupt payload: the
// reserved kind, a level above the shared maximum, or the spare nibble set.
[[nodiscard]] constexpr bool valid_packed_fluid(std::uint16_t packed) {
    if ((packed & 0x00F0U) != 0) {
        return false;
    }
    const auto kind = static_cast<FluidKind>((packed >> 14) & 0x3U);
    const auto level = static_cast<std::uint8_t>((packed >> 10) & 0xFU);
    if (kind == FluidKind::Reserved) {
        return false;
    }
    if (kind == FluidKind::None) {
        return level == 0 && (packed & 0x0F00U) == 0;
    }
    return level >= 1 && level <= kFluidMaxLevel;
}

// ── water parameters (docs/research/10 §2.3 / §3.2 / §4.2) ──────────────────

// §2.3: the scheduled-tick interval decides the propagation rate. Water: 5
// ticks per block (wiki Water infobox `flowrate = 5 ticks/block`,正文
// "1 block every 5 game ticks"). The same interval drives vertical
// propagation: the downward rate is NOT sourced (the wiki pages quoted in
// research/10 give the horizontal flowrate only), so the vertical column
// reuses the horizontal interval rather than inventing a number. Flagged as a
// calibration item in the T-F1 report.
inline constexpr std::uint64_t kFluidTickIntervalWater = 5;

// §3.2: "水 = 1" (wiki Fluid: water and Nether lava gain one level per flowing
// block; Overworld/End lava gains two).
inline constexpr int kFluidLevelDecayWater = 1;
inline constexpr int kFluidLevelDecayLavaNether = 1;
inline constexpr int kFluidLevelDecayLavaOverworld = 2;

// Horizontal reach implied by the recurrence of §3.2 (8 - decay * distance),
// not an independent rule: water 7 blocks, Overworld lava 3, Nether lava 7.
// Recorded for the acceptance assertions; the propagation itself is emergent.
inline constexpr int kFluidSpreadDistanceWater = 7;

// §4.2 slope-search radius.
//
// Taken from the wiki `Fluid` page body: "The area checked is up to 5 blocks
// away for water or lava in the Nether and up to 3 blocks away for lava
// elsewhere." The wiki `Water` page states the same preference differently
// ("reachable in four or fewer blocks from the block it wants to flow to"),
// which is consistent with this value only if that sentence counts from the
// first cell water flows into rather than from the flowing cell itself.
// Public sources cannot settle which reading matches 1.21.x, so the value is
// a named constant and stays flagged: debt T-D22 / T-R1 ruling 2 (待实机校准).
// Until that calibration happens this implementation must not be described as
// block-for-block identical to MC.
inline constexpr int kSlopeSearchRadiusWater = 5;
inline constexpr int kSlopeSearchRadiusLavaOverworld = 3;
inline constexpr int kSlopeSearchRadiusLavaNether = 5;

[[nodiscard]] constexpr std::uint64_t fluid_tick_interval(FluidKind kind) {
    switch (kind) {
    case FluidKind::Lava:
        return 30; // §2.3 Overworld/End; Nether is 10, stage 3
    case FluidKind::Water:
    case FluidKind::None:
    case FluidKind::Reserved:
    default:
        return kFluidTickIntervalWater;
    }
}

[[nodiscard]] constexpr int fluid_level_decay(FluidKind kind) {
    return kind == FluidKind::Lava ? kFluidLevelDecayLavaOverworld : kFluidLevelDecayWater;
}

[[nodiscard]] constexpr int fluid_slope_radius(FluidKind kind) {
    return kind == FluidKind::Lava ? kSlopeSearchRadiusLavaOverworld : kSlopeSearchRadiusWater;
}

// §1.3 render height: h(P) = level/9 for a flowing cell, 1 for a full cell
// (source or falling column). The 9 denominator is a reconstruction, not a
// wiki formula; it makes the outermost level-1 shell ~0.111 blocks thick.
[[nodiscard]] constexpr float fluid_render_height(std::uint8_t level) {
    if (level >= kFluidMaxLevel) {
        return 1.0f;
    }
    return static_cast<float>(level) / 9.0f;
}

} // namespace opencraft::voxel
