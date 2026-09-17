#include "asset_atlas.hpp"

#include "opencraft/core/log.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

// The stb_image implementation lives in this translation unit and nowhere else:
// a second definition would collide at link time. Only the PNG decoder is
// compiled in - load_tile_png() checks the file signature itself, so the other
// decoders would be dead weight - and the FILE* API is unused because the
// loader reads the bytes itself (that is what lets a missing file stay silent
// while a present-but-broken file is reported).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

namespace opencraft::client {

namespace {

// A 16x16 tile is a few hundred bytes; anything at this scale is not one of
// ours and must not be read into memory just to be rejected.
constexpr std::uintmax_t kMaxAssetBytes = 4U * 1024U * 1024U;

constexpr std::array<std::uint8_t, 8> kPngSignature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

// Same packing as atlas.cpp rgba() and AtlasImage::pixels.
std::uint32_t pack_rgba(const stbi_uc *rgba) {
    return (static_cast<std::uint32_t>(rgba[3]) << 24) | (static_cast<std::uint32_t>(rgba[2]) << 16) |
           (static_cast<std::uint32_t>(rgba[1]) << 8) | static_cast<std::uint32_t>(rgba[0]);
}

std::optional<std::vector<std::uint8_t>> read_file(const std::filesystem::path &path, std::string &why) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        why = "cannot read its size";
        return std::nullopt;
    }
    if (size > kMaxAssetBytes) {
        why = "file is far larger than a 16x16 tile";
        return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        why = "cannot open it";
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
        if (in.gcount() != static_cast<std::streamsize>(size)) {
            why = "read fewer bytes than the file claims";
            return std::nullopt;
        }
    }
    return bytes;
}

} // namespace

const char *tile_slot_name(int slot) {
    switch (slot) {
    case 0:
        return "top";
    case 1:
        return "side";
    case 2:
        return "bottom";
    default:
        return "invalid";
    }
}

std::filesystem::path block_tile_path(const std::filesystem::path &assets_root, std::string_view block_id, int slot) {
    return assets_root / "blocks" / (std::string(block_id) + "_" + tile_slot_name(slot) + ".png");
}

std::filesystem::path item_icon_path(const std::filesystem::path &assets_root, std::string_view item_id) {
    return assets_root / "items" / (std::string(item_id) + ".png");
}

std::optional<TilePixels> load_tile_png(const std::filesystem::path &path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        // Absent is the ordinary state of a fresh checkout, not a failure, so
        // it must not produce a warning.
        return std::nullopt;
    }

    std::string why;
    const std::optional<std::vector<std::uint8_t>> bytes = read_file(path, why);
    if (!bytes.has_value()) {
        OC_LOG_WARN("asset tile {} was rejected ({}); painting the procedural texture instead", path.string(), why);
        return std::nullopt;
    }
    if (bytes->size() < kPngSignature.size() ||
        !std::equal(kPngSignature.begin(), kPngSignature.end(), bytes->begin())) {
        OC_LOG_WARN("asset tile {} is not a PNG; painting the procedural texture instead", path.string());
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    stbi_uc *rgba = stbi_load_from_memory(bytes->data(), static_cast<int>(bytes->size()), &width, &height, nullptr, 4);
    if (rgba == nullptr) {
        OC_LOG_WARN("asset tile {} failed to decode ({}); painting the procedural texture instead", path.string(),
                    stbi_failure_reason());
        return std::nullopt;
    }
    if (width != kTileSize || height != kTileSize) {
        OC_LOG_WARN("asset tile {} is {}x{} px, expected {}x{}; painting the procedural texture instead", path.string(),
                    width, height, kTileSize, kTileSize);
        stbi_image_free(rgba);
        return std::nullopt;
    }

    TilePixels tile{};
    for (int row = 0; row < kTileSize; ++row) {
        // PNG row 0 is the visual top; atlas row 0 is the visual bottom of the
        // tile (paint_tile() documents the same convention), so rows flip.
        const stbi_uc *src = rgba + static_cast<std::size_t>(row) * kTileSize * 4;
        std::uint32_t *dst = tile.data() + static_cast<std::size_t>(kTileSize - 1 - row) * kTileSize;
        for (int col = 0; col < kTileSize; ++col) {
            dst[col] = pack_rgba(src + static_cast<std::size_t>(col) * 4);
        }
    }
    stbi_image_free(rgba);
    return tile;
}

std::optional<TilePixels> load_block_tile(const std::filesystem::path &assets_root, std::string_view block_id,
                                          int slot) {
    return load_tile_png(block_tile_path(assets_root, block_id, slot));
}

std::optional<TilePixels> load_item_icon(const std::filesystem::path &assets_root, std::string_view item_id) {
    return load_tile_png(item_icon_path(assets_root, item_id));
}

std::filesystem::path resolve_assets_root() {
    std::vector<std::filesystem::path> candidates;
    if (const char *override_dir = std::getenv("OPENCRAFT_ASSETS_DIR");
        override_dir != nullptr && *override_dir != '\0') {
        candidates.emplace_back(override_dir);
    }
    candidates.emplace_back("assets");
    candidates.emplace_back(std::filesystem::path("..") / "assets");
    return resolve_assets_root(candidates);
}

std::filesystem::path resolve_assets_root(const std::vector<std::filesystem::path> &candidates) {
    for (const std::filesystem::path &candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::is_directory(candidate, ec)) {
            return candidate.lexically_normal();
        }
    }
    return {};
}

} // namespace opencraft::client
