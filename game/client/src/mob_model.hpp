#pragma once

// T-B1: the mob model channel - `assets/mobs/<mob_id>.vox` (docs/research/
// 12-mob-model-formats.md §3, §5, §6.5).
//
// The format is small enough to read ourselves - the whole point of Q4: a
// decoder for a fixed-size little-endian container beats pulling in a library
// that carries four third-party codecs with it. Everything in this header is
// pure: no GL, no logging on the hot path, no filesystem access in parse_vox().
// That is what lets tests/test_mob_model.cpp link the client's own decoder.
//
// Fallback contract (§6.5, one row per state, nothing else is allowed to
// differ): a missing file is silent, a present-but-unusable file is one WARN,
// and in both cases the caller draws the two boxes it drew before this channel
// existed.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opencraft::client {

// Palette entries in a .vox (and therefore cells in a palette PNG, §3.3).
inline constexpr std::size_t kMobPaletteSize = 256;

// A SIZE component is signed 32-bit in the file but a voxel coordinate is one
// byte, so the only grids that can describe their own voxels are 1..256.
inline constexpr int kMobGridMaxSide = 256;

// Why a model was declined, for the single WARN line. Reasons are stable
// strings so tests can assert on them.
enum class VoxError {
    None = 0,
    NotVoxFile,     // wrong signature - "this is not a .vox"
    Truncated,      // a chunk header or content runs past the end of the file
    BadChunk,       // a chunk we need is present but too small to be it
    BadGridSize,    // SIZE outside 1..256
    MissingChunk,   // no SIZE or no XYZI
    MissingPalette, // no RGBA chunk, so no colors to draw with
    VoxelCountLies, // XYZI claims more voxels than its own content holds
    VoxelOutOfRange // a voxel coordinate is outside SIZE
};

[[nodiscard]] const char *vox_error_reason(VoxError error);

struct VoxVoxel {
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t z = 0;
    // 1-based: colorIndex N describes palette entry N-1 (research/12 §5.2(a)).
    std::uint8_t color = 0;
};

struct VoxModel {
    // SIZE, in voxels. MagicaVoxel's z is up.
    int size_x = 0;
    int size_y = 0;
    int size_z = 0;
    std::vector<VoxVoxel> voxels;
    // palette[colorIndex] is that index's RGBA8, so index 0 is unused and
    // entries are 1-based the way the file's own indices are. Packed like
    // AtlasImage::pixels (a<<24|b<<16|g<<8|r) so it can go straight to the GPU.
    std::array<std::uint32_t, kMobPaletteSize> palette{};
};

// Parses one model out of `bytes`.
//
// A file may hold several models (a PACK chunk, the 0.98.2 animation layout we
// deliberately do not support, §4.2): the first SIZE/XYZI pair wins and the
// rest are ignored, but their presence never fails the parse.
//
// A syntactically valid file with zero voxels parses FINE and returns a model
// with an empty voxel list - "there is no geometry here" is a content mistake
// the loader reports, not a format error (§5.4 item 4).
[[nodiscard]] std::optional<VoxModel> parse_vox(const std::uint8_t *bytes, std::size_t size, VoxError &error);

// ── the asset channel ───────────────────────────────────────────────────────

// <assets_root>/mobs/<mob_id>.vox, same shape as block_tile_path().
[[nodiscard]] std::filesystem::path mob_model_path(const std::filesystem::path &assets_root, std::string_view mob_id);

// <assets_root>/palettes/<mob_id>.png - the §3.3 color override: 16x16 with one
// cell per colorIndex, the pixel at (col, row) counted from the image's
// top-left holding colorIndex row*16+col. Cell 1 is therefore the first joint
// label and cell 0 is the never-painted index 0 - the PNG's cell number IS the
// index, with none of the file's "+1" shift.
[[nodiscard]] std::filesystem::path mob_palette_path(const std::filesystem::path &assets_root, std::string_view mob_id);

// One loaded mob: the parsed file plus the palette it will actually be drawn
// with.
struct MobModel {
    std::string id;
    VoxModel vox;
    // palette[colorIndex] after the palette-PNG override, if there was one.
    std::array<std::uint32_t, kMobPaletteSize> palette{};
    // True when a palette PNG was found and used (the §3.3 override).
    bool palette_from_png = false;
};

// Reads `mob_ids` out of <assets_root>/mobs, applying the §6.5 table:
//   * directory or file absent          -> skipped, silent
//   * present but not a .vox / unparsable / 0 voxels -> skipped + one WARN
//   * palette PNG absent                -> the file's own palette, silent
//   * palette PNG present but unusable  -> the file's own palette + one WARN
// Never throws.
//
// It also emits the startup count, the machine criterion for "the channel is
// live" - the sibling of atlas.cpp's "atlas: N/M block tiles loaded":
//
//     mobs: N/M mob models loaded from <assets_root>/mobs
//
// `mob_ids` is the roster (MobRegistry), not a hard-coded list, so the
// denominator stays right when a mob is added. Like the atlas line it is
// skipped entirely when `assets_root` is empty - "no asset tree at all" is the
// fresh-checkout state and T-A2 already established that it stays quiet.
[[nodiscard]] std::vector<MobModel> load_mob_models(const std::filesystem::path &assets_root,
                                                    const std::vector<std::string> &mob_ids);

// Joint labels (research/12 §4.3): colorIndex 1..8 name a joint AND supply that
// joint's color. A voxel painted with 9..255 has no label and belongs to the
// body.
inline constexpr std::size_t kMobJointCount = 8;

} // namespace opencraft::client
