#pragma once

// T-A2: the external art-asset channel. Block textures may be authored as PNG
// files under assets/; anything absent or unusable falls back to the
// procedural patterns in atlas.cpp, which stay the reference for what the game
// looked like before there were any asset files (docs/tasks/T-A2.md §3.3).
//
// Two rules are deliberately strict, because a silently-wrong texture is worse
// than a missing one:
//   * a tile must be exactly kTileSize x kTileSize pixels - no rescaling
//     (interpolated pixel art hides an authoring mistake and cannot be gated);
//   * the file must really be a PNG, checked by signature, not merely
//     "something stb_image happens to decode".
// Everything else is lenient: any colour type or bit depth stb_image can read
// is decoded to RGBA8.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace opencraft::client {

// Side length of one atlas tile in pixels. Frozen with the atlas index contract
// (engine/render/include/opencraft/render/mesher.hpp tile_index(), docs/tasks/
// T-A2.md §3.2); an asset file of another size is rejected, never scaled.
inline constexpr int kTileSize = 16;
inline constexpr std::size_t kTilePixelCount = static_cast<std::size_t>(kTileSize) * kTileSize;

// One decoded tile, packed exactly like AtlasImage::pixels (a<<24|b<<16|g<<8|r)
// and stored in the atlas' row order: row 0 is the *bottom* of the image. A
// block-face top edge samples the tile's highest row (see paint_tile() in
// atlas.cpp), so PNG rows - which start at the image top - arrive flipped and
// are flipped back while decoding.
using TilePixels = std::array<std::uint32_t, kTilePixelCount>;

// Block texture location, frozen by docs/tasks/T-A2.md §3.1:
//   <assets_root>/blocks/<block_id>_<slot>.png
// `slot`: 0 = top, 1 = side, 2 = bottom (the mesher's slots).
[[nodiscard]] std::filesystem::path block_tile_path(const std::filesystem::path &assets_root, std::string_view block_id,
                                                    int slot);

// Item-icon location, the second channel of the same pipeline:
//   <assets_root>/items/<item_id>.png
// T-A2 only opens this path - nothing loads icons yet, the HUD still uses the
// flat tints of inventory_wiring.hpp item_tint(). It lives here so the card
// that does draw icons does not have to invent a second convention.
[[nodiscard]] std::filesystem::path item_icon_path(const std::filesystem::path &assets_root, std::string_view item_id);

// Slot name as it appears in a file name; an out-of-range slot answers
// "invalid", which matches no file (callers pass the mesher's 0..2).
[[nodiscard]] const char *tile_slot_name(int slot);

// Reads one 16x16 RGBA8 tile from `path`. Never throws and never fatal:
//   * file absent                        -> nullopt, silent (the normal case)
//   * present, but not a PNG             -> nullopt + OC_LOG_WARN once
//   * present, PNG, but not 16x16        -> nullopt + OC_LOG_WARN once
//   * present, PNG, 16x16, undecodable   -> nullopt + OC_LOG_WARN once
[[nodiscard]] std::optional<TilePixels> load_tile_png(const std::filesystem::path &path);

// The same, addressed by block id + slot (a thin path builder).
[[nodiscard]] std::optional<TilePixels> load_block_tile(const std::filesystem::path &assets_root,
                                                        std::string_view block_id, int slot);

// The same, addressed by item id - the reserved icon channel.
[[nodiscard]] std::optional<TilePixels> load_item_icon(const std::filesystem::path &assets_root,
                                                       std::string_view item_id);

// Locates the asset tree at runtime, first candidate that is a directory wins:
//   1. $OPENCRAFT_ASSETS_DIR (explicit override)
//   2. <cwd>/assets     - the documented launch, ./build/opencraft from the
//                         repo root (game/client/CMakeLists.txt)
//   3. <cwd>/../assets  - the same binary started from inside build/, which is
//                         how the saves/ tree is created (see main.cpp)
// An empty result means "no asset tree": every tile then falls back silently
// (docs/tasks/T-A2.md §3.5), which is also the state of a fresh checkout.
[[nodiscard]] std::filesystem::path resolve_assets_root();

// The same search with an explicit candidate list, in order; pure with respect
// to the filesystem apart from the existence checks (used by tests and tools).
[[nodiscard]] std::filesystem::path resolve_assets_root(const std::vector<std::filesystem::path> &candidates);

} // namespace opencraft::client
