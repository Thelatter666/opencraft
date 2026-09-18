#include "mob_model.hpp"

#include "asset_atlas.hpp" // load_tile_png / TilePixels - the palette override

#include "opencraft/core/log.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace opencraft::client {

namespace {

// A 32^3 grid is at most ~33k cells, and the art budget tops out near 1000
// voxels (research/12 §7.2), so a file this large is not one of ours. The cap
// only exists so a wrong path cannot make us read a disc image into memory.
constexpr std::uintmax_t kMaxVoxBytes = 16U * 1024U * 1024U;

constexpr std::size_t kChunkHeaderBytes = 12;

std::uint32_t read_u32(const std::uint8_t *p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::int32_t read_i32(const std::uint8_t *p) {
    return static_cast<std::int32_t>(read_u32(p));
}

bool chunk_id_is(const std::uint8_t *p, const char *id) {
    return std::memcmp(p, id, 4) == 0;
}

std::optional<std::vector<std::uint8_t>> read_whole_file(const std::filesystem::path &path, std::string &why) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        why = "cannot read its size";
        return std::nullopt;
    }
    if (size > kMaxVoxBytes) {
        why = "file is far larger than a mob model";
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

// The palette PNG is loaded through the atlas channel's 16x16 loader, which
// returns rows bottom-up (its own documented convention for block tiles). A
// palette PNG has 256 CELLS, one per colorIndex: the pixel at (col, row)
// counted from the image's top-left holds colorIndex row*16+col, so cell 1 is
// colorIndex 1 and cell 0 - like the .vox's own index 0 - is never painted.
//
// ⚠ Deliberately NOT the file's "+1" shift: the RGBA chunk stores colorIndex N
// at entry N-1, and that shift happens exactly once, in parse_vox(). Repeating
// it here would put the picture the artist edits one cell off from the indices
// they paint with. After the row flip, texture row t holds colorIndices
// 16t..16t+15 - which is exactly the texel a vertex with that colorIndex
// samples.
std::optional<std::array<std::uint32_t, kMobPaletteSize>> load_palette_png(const std::filesystem::path &path) {
    const std::optional<TilePixels> tile = load_tile_png(path);
    if (!tile.has_value()) {
        return std::nullopt;
    }
    std::array<std::uint32_t, kMobPaletteSize> palette{};
    for (std::size_t color = 1; color < kMobPaletteSize; ++color) {
        const int row = static_cast<int>(color / kTileSize);
        const int col = static_cast<int>(color % kTileSize);
        palette[color] =
            (*tile)[static_cast<std::size_t>(kTileSize - 1 - row) * kTileSize + static_cast<std::size_t>(col)];
    }
    return palette;
}

} // namespace

const char *vox_error_reason(VoxError error) {
    switch (error) {
    case VoxError::None:
        return "no error";
    case VoxError::NotVoxFile:
        return "missing the VOX signature";
    case VoxError::Truncated:
        return "a chunk runs past the end of the file";
    case VoxError::BadChunk:
        return "a chunk is too small to hold what it declares";
    case VoxError::BadGridSize:
        return "SIZE is outside 1..256";
    case VoxError::MissingChunk:
        return "no SIZE or no XYZI chunk";
    case VoxError::MissingPalette:
        return "no RGBA chunk";
    case VoxError::VoxelCountLies:
        return "XYZI claims more voxels than its content holds";
    case VoxError::VoxelOutOfRange:
        return "a voxel coordinate is outside SIZE";
    }
    return "unknown";
}

std::optional<VoxModel> parse_vox(const std::uint8_t *bytes, std::size_t size, VoxError &error) {
    error = VoxError::NotVoxFile;
    if (bytes == nullptr || size < 8 || !chunk_id_is(bytes, "VOX ")) {
        return std::nullopt;
    }

    VoxModel model;
    std::vector<VoxVoxel> voxels;
    bool have_size = false;
    bool have_voxels = false;
    bool have_palette = false;

    std::size_t offset = 8; // "VOX " + version
    while (offset + kChunkHeaderBytes <= size) {
        const std::uint8_t *header = bytes + offset;
        const std::int32_t content_size = read_i32(header + 4);
        // MAIN's declared children size spans the whole file, so it cannot be
        // used to step past a chunk: every chunk we care about is a child of
        // MAIN and they are laid out consecutively, which makes a flat walk
        // exactly right (and immune to a lying children_size).
        if (content_size < 0 || static_cast<std::size_t>(content_size) > size - offset - kChunkHeaderBytes) {
            error = VoxError::Truncated;
            return std::nullopt;
        }
        const std::uint8_t *content = header + kChunkHeaderBytes;
        const auto content_len = static_cast<std::size_t>(content_size);

        if (chunk_id_is(header, "SIZE") && !have_size) {
            if (content_len < 12) {
                error = VoxError::BadChunk;
                return std::nullopt;
            }
            model.size_x = read_i32(content);
            model.size_y = read_i32(content + 4);
            model.size_z = read_i32(content + 8);
            if (model.size_x < 1 || model.size_y < 1 || model.size_z < 1 || model.size_x > kMobGridMaxSide ||
                model.size_y > kMobGridMaxSide || model.size_z > kMobGridMaxSide) {
                error = VoxError::BadGridSize;
                return std::nullopt;
            }
            have_size = true;
        } else if (chunk_id_is(header, "XYZI") && !have_voxels) {
            if (content_len < 4) {
                error = VoxError::BadChunk;
                return std::nullopt;
            }
            const std::uint32_t count = read_u32(content);
            if (static_cast<std::size_t>(count) > (content_len - 4) / 4) {
                error = VoxError::VoxelCountLies;
                return std::nullopt;
            }
            voxels.clear();
            voxels.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                const std::uint8_t *v = content + 4 + static_cast<std::size_t>(i) * 4;
                voxels.push_back(VoxVoxel{v[0], v[1], v[2], v[3]});
            }
            have_voxels = true;
        } else if (chunk_id_is(header, "RGBA") && !have_palette) {
            // The format says 256 entries. We accept FEWER: a minimal writer
            // (a hand-authored model, a small generator) reasonably emits only
            // the colors it uses, and the entries it left out are "unused" -
            // exactly what a zeroed entry means here anyway (alpha 0, so the
            // fragment shader discards it). Truncation is still caught: a chunk
            // that declares more bytes than the file holds fails above, on
            // content_size. Zero entries is a chunk with no palette at all.
            if (content_len < 4) {
                error = VoxError::BadChunk;
                return std::nullopt;
            }
            const std::size_t entries = std::min(kMobPaletteSize, content_len / 4);
            for (std::size_t i = 0; i < entries; ++i) {
                // Entry e (0-based in the chunk) is colorIndex e+1; index 0 is
                // never painted, so it stays zeroed.
                if (i + 1 < kMobPaletteSize) {
                    const std::uint8_t *c = content + i * 4;
                    model.palette[i + 1] = (static_cast<std::uint32_t>(c[3]) << 24) |
                                           (static_cast<std::uint32_t>(c[2]) << 16) |
                                           (static_cast<std::uint32_t>(c[1]) << 8) | static_cast<std::uint32_t>(c[0]);
                }
            }
            have_palette = true;
        }
        // Every other chunk (MATT, PACK, nTRN/nSHP scene graph, ...) carries
        // nothing we draw and is stepped over.

        offset += kChunkHeaderBytes + content_len;
    }

    if (!have_size || !have_voxels) {
        error = VoxError::MissingChunk;
        return std::nullopt;
    }
    if (!have_palette) {
        error = VoxError::MissingPalette;
        return std::nullopt;
    }
    for (const VoxVoxel &v : voxels) {
        if (v.x >= model.size_x || v.y >= model.size_y || v.z >= model.size_z) {
            error = VoxError::VoxelOutOfRange;
            return std::nullopt;
        }
    }
    model.voxels = std::move(voxels);
    error = VoxError::None;
    return model;
}

std::filesystem::path mob_model_path(const std::filesystem::path &assets_root, std::string_view mob_id) {
    return assets_root / "mobs" / (std::string(mob_id) + ".vox");
}

std::filesystem::path mob_palette_path(const std::filesystem::path &assets_root, std::string_view mob_id) {
    return assets_root / "palettes" / (std::string(mob_id) + ".png");
}

std::vector<MobModel> load_mob_models(const std::filesystem::path &assets_root,
                                      const std::vector<std::string> &mob_ids) {
    std::vector<MobModel> loaded;
    if (assets_root.empty()) {
        return loaded;
    }

    for (const std::string &id : mob_ids) {
        const std::filesystem::path path = mob_model_path(assets_root, id);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            continue; // absent is the normal state of a fresh checkout: no log
        }

        std::string why;
        const std::optional<std::vector<std::uint8_t>> bytes = read_whole_file(path, why);
        if (!bytes.has_value()) {
            OC_LOG_WARN("mob model {} was rejected ({}); drawing {} as boxes instead", path.string(), why, id);
            continue;
        }
        VoxError error = VoxError::None;
        std::optional<VoxModel> model = parse_vox(bytes->data(), bytes->size(), error);
        if (!model.has_value()) {
            OC_LOG_WARN("mob model {} was rejected ({}); drawing {} as boxes instead", path.string(),
                        vox_error_reason(error), id);
            continue;
        }
        if (model->voxels.empty()) {
            // A valid file with nothing in it is an authoring mistake, not a
            // normal state, so unlike "file absent" it speaks up.
            OC_LOG_WARN("mob model {} holds 0 voxels; drawing {} as boxes instead", path.string(), id);
            continue;
        }

        MobModel entry;
        entry.id = id;
        entry.palette = model->palette;
        const std::filesystem::path palette_path = mob_palette_path(assets_root, id);
        if (const std::optional<std::array<std::uint32_t, kMobPaletteSize>> from_png = load_palette_png(palette_path);
            from_png.has_value()) {
            entry.palette = *from_png;
            entry.palette_from_png = true;
        }
        entry.vox = std::move(*model);
        loaded.push_back(std::move(entry));
    }

    OC_LOG_INFO("mobs: {}/{} mob models loaded from {}/mobs", loaded.size(), mob_ids.size(), assets_root.string());
    return loaded;
}

} // namespace opencraft::client
