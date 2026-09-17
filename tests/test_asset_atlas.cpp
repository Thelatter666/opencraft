// T-A2: the external art-asset pipeline (docs/tasks/T-A2.md).
//
// The acceptance core is "with no asset files present the atlas is
// byte-identical to what the procedural generator produced before this channel
// existed". That is asserted here against per-tile FNV-1a 64 digests captured
// from the pre-change generator (see kGoldenTiles) rather than against a
// second call into the same code, so the test can actually fail.
//
// The PNG fixtures are written by the small encoder at the bottom of this
// file, not read from committed blobs: it shares no code with stb_image (the
// decoder under test), so a fixture cannot agree with the decoder by
// construction.

#include <doctest/doctest.h>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "asset_atlas.hpp"
#include "atlas.hpp"

#include "opencraft/render/mesher.hpp"
#include "opencraft/voxel/block_registry.hpp"

namespace {

using opencraft::client::AtlasImage;
using opencraft::client::kTileSize;
using opencraft::voxel::BlockRegistry;

// ── digests ─────────────────────────────────────────────────────────────────

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t fnv_tile(const AtlasImage &atlas, std::size_t tile) {
    const int tx = static_cast<int>(tile % static_cast<std::size_t>(atlas.tiles_per_row)) * kTileSize;
    const int ty = static_cast<int>(tile / static_cast<std::size_t>(atlas.tiles_per_row)) * kTileSize;
    std::uint64_t h = kFnvOffset;
    for (int py = 0; py < kTileSize; ++py) {
        const std::uint32_t *row = atlas.pixels.data() + static_cast<std::size_t>(ty + py) * atlas.width + tx;
        for (int px = 0; px < kTileSize; ++px) {
            const std::uint32_t v = row[px];
            for (int shift = 0; shift < 32; shift += 8) {
                h ^= static_cast<std::uint64_t>((v >> shift) & 0xFFU);
                h *= kFnvPrime;
            }
        }
    }
    return h;
}

std::uint64_t fnv_whole(const AtlasImage &atlas) {
    std::uint64_t h = kFnvOffset;
    for (std::uint32_t v : atlas.pixels) {
        for (int shift = 0; shift < 32; shift += 8) {
            h ^= static_cast<std::uint64_t>((v >> shift) & 0xFFU);
            h *= kFnvPrime;
        }
    }
    return h;
}

// Per-tile digests of the pre-T-A2 atlas, produced by compiling atlas.cpp from
// commit bc20aab (the parent of this task's work) into a standalone dumper and
// printing the same FNV-1a 64 over each 16x16 tile, in tile-index order, for
// BlockRegistry::create_default() (21 entries: air + 20 content blocks; 81
// tiles of a 9x9 grid, of which 73 are painted and 8 stay background).
// The atlas layout is frozen by docs/tasks/T-A2.md §3.2, so a mismatch here is
// a regression in the procedural textures, not a deliberate change.
constexpr std::uint64_t kGoldenWhole = 0x334e2cce2595568dULL;
constexpr std::array<std::uint64_t, 81> kGoldenTiles{
    0x33c8e3e71aff253eULL, 0x4968261bf8b3105cULL, 0x6c62b73fb2ab6a15ULL, 0x1f794b846c229467ULL, 0x3ce595bb0df962a4ULL,
    0x60d9f08ed9286e0cULL, 0x8d88e99e11199af9ULL, 0x49ec20df3e96789fULL, 0xc302d33ea10920c6ULL, 0xe60bef540b6330cdULL,
    0x28f2c1147c56091aULL, 0xeab10b305cbed8baULL, 0x25d47d6ff1c0f763ULL, 0xfa2903897fb80ae3ULL, 0x3407585852f4fee3ULL,
    0xd7720340be2fef23ULL, 0x21ddb00903ba8fbfULL, 0x97b535f7ef7e013dULL, 0x0ead52c4266d0543ULL, 0x6e5608662ba583cdULL,
    0x07b4ccaf827e36ceULL, 0x34539a65719d0c0eULL, 0x5c610b5597e92283ULL, 0xd9ae0f483d8a3ad4ULL, 0x9c6a61dcace13f1cULL,
    0xa4a08ec0ac627abbULL, 0xedeeeb3b62ab5374ULL, 0xfd6078936ff896d6ULL, 0x5b08bc58ee20f691ULL, 0x2690e9fb03d4b24cULL,
    0xea3aa660faed467aULL, 0x7b259b7003bc1aabULL, 0x048c1fa61fec5660ULL, 0x20f881fe0cd78103ULL, 0x20f881fe0cd78103ULL,
    0x20f881fe0cd78103ULL, 0x0e4a5417550aab83ULL, 0x0e4a5417550aab83ULL, 0x0e4a5417550aab83ULL, 0xf1992b747164817bULL,
    0xe8c480b121cd9f57ULL, 0xd00415a25aafd72bULL, 0x91bbbaadce69f30bULL, 0xdfe75d6ad42771ddULL, 0x7f532d2860a2f62eULL,
    0x9d259ae6d68384f8ULL, 0x815c11b21c1f93d2ULL, 0xef4319245cef118fULL, 0xb65af30a938f1fb6ULL, 0x99ca1100c8f01812ULL,
    0x029f43723f7c9205ULL, 0xf55aa90d3a2c3c9bULL, 0x60f10190272085b0ULL, 0xf1455205c53450e8ULL, 0x943b701abd2cbddbULL,
    0xfbe1f0ecc99649f2ULL, 0xd7343709342d3ce9ULL, 0x7d421a40ea80cb85ULL, 0x18632c1b1c87b353ULL, 0x67044b5fa1ca251dULL,
    0xee329061aa2569aaULL, 0x10981c7eb52e4825ULL, 0xf52566fcecee5d12ULL, 0x0c18fd965a482353ULL, 0xbbf067ae7480c553ULL,
    0x5897c692b25ac783ULL, 0x195392461912e863ULL, 0xc873ce99e5b73893ULL, 0x062f43a19b0022b3ULL, 0xb65b9e9cd2309b33ULL,
    0xad3b110c8b1c606fULL, 0xbd6c9735cd5a8ca3ULL, 0xb05478f09e6c79d3ULL, 0x47b92fe7712ea383ULL, 0x47b92fe7712ea383ULL,
    0x47b92fe7712ea383ULL, 0x47b92fe7712ea383ULL, 0x47b92fe7712ea383ULL, 0x47b92fe7712ea383ULL, 0x47b92fe7712ea383ULL,
    0x47b92fe7712ea383ULL,
};

// ── helpers ─────────────────────────────────────────────────────────────────

constexpr std::uint32_t rgba(int r, int g, int b, int a = 255) {
    return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(r);
}

constexpr std::uint32_t kMagenta = rgba(255, 0, 255);
constexpr std::uint32_t kCyan = rgba(0, 255, 255);
constexpr std::uint32_t kRed = rgba(220, 30, 30);
constexpr std::uint32_t kGreen = rgba(30, 220, 30);
constexpr std::uint32_t kBlue = rgba(30, 30, 220);

// A scratch directory that removes itself, so no test leaves state behind.
class TempDir {
public:
    TempDir() {
        static int counter = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("opencraft_ta2_" + std::to_string(stamp) + "_" + std::to_string(counter++));
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    REQUIRE(std::filesystem::is_regular_file(path));
}

// `pixels` is row-major from the image's top row, one 0xRRGGBBAA per pixel.
std::vector<std::uint32_t> solid(int width, int height, std::uint32_t color) {
    return std::vector<std::uint32_t>(static_cast<std::size_t>(width) * height, color);
}

// The same pixels in atlas storage order: PNG row r belongs to atlas row
// (kTileSize - 1 - r) because a block face's top edge samples the tile's
// highest row (paint_tile() in atlas.cpp).
std::array<std::uint32_t, opencraft::client::kTilePixelCount>
atlas_order(const std::vector<std::uint32_t> &png_top_down) {
    std::array<std::uint32_t, opencraft::client::kTilePixelCount> out{};
    for (int row = 0; row < kTileSize; ++row) {
        for (int col = 0; col < kTileSize; ++col) {
            out[static_cast<std::size_t>(kTileSize - 1 - row) * kTileSize + col] =
                png_top_down[static_cast<std::size_t>(row) * kTileSize + col];
        }
    }
    return out;
}

std::size_t tile_differences(const AtlasImage &atlas, std::size_t tile,
                             const std::array<std::uint32_t, opencraft::client::kTilePixelCount> &expected) {
    const int tx = static_cast<int>(tile % static_cast<std::size_t>(atlas.tiles_per_row)) * kTileSize;
    const int ty = static_cast<int>(tile / static_cast<std::size_t>(atlas.tiles_per_row)) * kTileSize;
    std::size_t diffs = 0;
    for (int py = 0; py < kTileSize; ++py) {
        for (int px = 0; px < kTileSize; ++px) {
            const std::uint32_t got = atlas.pixels[static_cast<std::size_t>(ty + py) * atlas.width + tx + px];
            if (got != expected[static_cast<std::size_t>(py) * kTileSize + px]) {
                ++diffs;
            }
        }
    }
    return diffs;
}

// Counts the tiles that differ from the frozen pre-T-A2 digests, ignoring
// `skip` (the tile a test deliberately overrode).
std::size_t tiles_off_golden(const AtlasImage &atlas, std::size_t skip) {
    std::size_t off = 0;
    for (std::size_t tile = 0; tile < kGoldenTiles.size(); ++tile) {
        if (tile == skip) {
            continue;
        }
        if (fnv_tile(atlas, tile) != kGoldenTiles[tile]) {
            ++off;
        }
    }
    return off;
}

// Collects spdlog warnings for the duration of a scope, so "OC_LOG_WARN and
// fall back" (docs/tasks/T-A2.md §3.5) is an assertion and not a hope.
class CaptureWarnings {
public:
    CaptureWarnings() : logger_(spdlog::default_logger()), previous_level_(logger_->level()) {
        logger_->set_level(spdlog::level::warn);
        logger_->sinks().push_back(sink_);
    }

    ~CaptureWarnings() {
        auto &sinks = logger_->sinks();
        const auto it = std::find_if(sinks.begin(), sinks.end(), [this](const std::shared_ptr<spdlog::sinks::sink> &s) {
            return s.get() == sink_.get();
        });
        if (it != sinks.end()) {
            sinks.erase(it);
        }
        logger_->set_level(previous_level_);
    }

    CaptureWarnings(const CaptureWarnings &) = delete;
    CaptureWarnings &operator=(const CaptureWarnings &) = delete;

    [[nodiscard]] std::size_t count() const { return sink_->messages().size(); }

    [[nodiscard]] bool mentions(const std::string &needle) const {
        const std::vector<std::string> &messages = sink_->messages();
        return std::any_of(messages.begin(), messages.end(),
                           [&needle](const std::string &m) { return m.find(needle) != std::string::npos; });
    }

private:
    class Sink final : public spdlog::sinks::base_sink<std::mutex> {
    public:
        [[nodiscard]] const std::vector<std::string> &messages() const { return messages_; }

    protected:
        void sink_it_(const spdlog::details::log_msg &msg) override {
            messages_.emplace_back(msg.payload.data(), msg.payload.size());
        }

        void flush_() override {}

    private:
        std::vector<std::string> messages_;
    };

    std::shared_ptr<spdlog::logger> logger_;
    std::shared_ptr<Sink> sink_ = std::make_shared<Sink>();
    spdlog::level::level_enum previous_level_ = spdlog::level::info;
};

// ── a minimal PNG encoder for the fixtures ──────────────────────────────────

std::uint32_t crc32(const std::uint8_t *data, std::size_t size) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit) {
                c = (c & 1U) != 0U ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        c = table[(c ^ data[i]) & 0xFFU] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFU;
}

void push_be32(std::vector<std::uint8_t> &out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void push_chunk(std::vector<std::uint8_t> &out, const char *type, const std::vector<std::uint8_t> &data) {
    push_be32(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t crc_start = out.size();
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(type[i]));
    }
    out.insert(out.end(), data.begin(), data.end());
    push_be32(out, crc32(out.data() + crc_start, out.size() - crc_start));
}

// zlib stream whose deflate blocks are stored (uncompressed): legal, tiny to
// write, and it keeps this encoder independent of any compression library.
std::vector<std::uint8_t> deflate_stored(const std::vector<std::uint8_t> &raw) {
    std::vector<std::uint8_t> out{0x78, 0x01};
    constexpr std::size_t kBlock = 65535;
    std::size_t offset = 0;
    do {
        const std::size_t n = std::min(kBlock, raw.size() - offset);
        const bool last = offset + n >= raw.size();
        out.push_back(last ? 0x01 : 0x00);
        out.push_back(static_cast<std::uint8_t>(n & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFU));
        const auto nlen = static_cast<std::uint16_t>(~static_cast<std::uint16_t>(n));
        out.push_back(static_cast<std::uint8_t>(nlen & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xFFU));
        out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                   raw.begin() + static_cast<std::ptrdiff_t>(offset + n));
        offset += n;
    } while (offset < raw.size());

    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::uint8_t byte : raw) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    push_be32(out, (b << 16) | a);
    return out;
}

std::vector<std::uint8_t> make_png(int width, int height, const std::vector<std::uint32_t> &pixels_top_down) {
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + width * 4));
    for (int row = 0; row < height; ++row) {
        raw.push_back(0); // filter type 0 (None)
        for (int col = 0; col < width; ++col) {
            // Colours arrive in the codebase's own packing (rgba(), a<<24|b<<16|g<<8|r)
            // and go out as the PNG wants them: R, G, B, A bytes.
            const std::uint32_t p = pixels_top_down[static_cast<std::size_t>(row) * width + col];
            raw.push_back(static_cast<std::uint8_t>(p));
            raw.push_back(static_cast<std::uint8_t>(p >> 8));
            raw.push_back(static_cast<std::uint8_t>(p >> 16));
            raw.push_back(static_cast<std::uint8_t>(p >> 24));
        }
    }

    std::vector<std::uint8_t> png{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    push_be32(ihdr, static_cast<std::uint32_t>(width));
    push_be32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // colour type: truecolour with alpha
    ihdr.push_back(0); // compression
    ihdr.push_back(0); // filter
    ihdr.push_back(0); // interlace
    push_chunk(png, "IHDR", ihdr);
    push_chunk(png, "IDAT", deflate_stored(raw));
    push_chunk(png, "IEND", {});
    return png;
}

} // namespace

// ── the frozen layout ───────────────────────────────────────────────────────

TEST_CASE("atlas tile layout and size formula are untouched by the asset channel") {
    const BlockRegistry registry = BlockRegistry::create_default();
    const std::filesystem::path absent = "opencraft_ta2_no_such_assets_dir";

    const AtlasImage atlas = opencraft::client::generate_atlas(registry, absent);

    // tile_count = size * 3 + 10 -> 73 -> ceil(sqrt(73)) = 9 tiles per row.
    // The default registry holds 21 entries - air plus 20 content blocks - so
    // there are 63 block tiles followed by the 10 crack stages.
    CHECK(registry.size() == std::size_t{21});
    CHECK(atlas.tiles_per_row == 9);
    CHECK(atlas.width == 144);
    CHECK(atlas.height == 144);
    CHECK(atlas.pixels.size() == 20736);

    for (std::uint16_t id = 0; id < registry.size(); ++id) {
        for (int slot = 0; slot < 3; ++slot) {
            // engine/render/mesher.hpp:157, frozen by docs/tasks/T-A2.md §3.2.
            CHECK(opencraft::render::tile_index(id, slot) == id * 3 + slot);
        }
    }
    CHECK(opencraft::client::crack_tile_base(registry.size()) == 63);
    CHECK(static_cast<std::size_t>(opencraft::client::crack_tile_base(registry.size())) + 10 <= 81);
}

TEST_CASE("asset file names follow the frozen directory layout") {
    const std::filesystem::path root = "assets";
    CHECK(opencraft::client::block_tile_path(root, "dirt", 1) == std::filesystem::path("assets/blocks/dirt_side.png"));
    CHECK(opencraft::client::block_tile_path(root, "grass_block", 0) ==
          std::filesystem::path("assets/blocks/grass_block_top.png"));
    CHECK(opencraft::client::block_tile_path(root, "grass_block", 2) ==
          std::filesystem::path("assets/blocks/grass_block_bottom.png"));
    CHECK(opencraft::client::item_icon_path(root, "sunroot") == std::filesystem::path("assets/items/sunroot.png"));

    CHECK(std::string(opencraft::client::tile_slot_name(0)) == "top");
    CHECK(std::string(opencraft::client::tile_slot_name(1)) == "side");
    CHECK(std::string(opencraft::client::tile_slot_name(2)) == "bottom");
}

// ── the acceptance core: no assets -> byte-identical to before ──────────────

TEST_CASE("with no asset files every tile is the pre-T-A2 procedural result") {
    const BlockRegistry registry = BlockRegistry::create_default();
    const TempDir temp;

    // Both a directory that exists but has no blocks/ inside and one that does
    // not exist at all must fall back for every tile.
    const AtlasImage atlas = opencraft::client::generate_atlas(registry, temp.path());
    const AtlasImage missing = opencraft::client::generate_atlas(registry, temp.path() / "not_there");

    CHECK(fnv_whole(atlas) == kGoldenWhole);
    CHECK(fnv_whole(missing) == kGoldenWhole);
    CHECK(atlas.pixels == missing.pixels);
    CHECK(tiles_off_golden(atlas, kGoldenTiles.size()) == 0);
}

TEST_CASE("an empty assets root is reported but never turns into an error") {
    const BlockRegistry registry = BlockRegistry::create_default();
    const TempDir temp;

    CaptureWarnings warnings;
    const AtlasImage atlas = opencraft::client::generate_atlas(registry, temp.path() / "missing");

    CHECK(warnings.count() == 0);
    CHECK(fnv_whole(atlas) == kGoldenWhole);
}

// ── the file channel actually feeds the atlas ──────────────────────────────

TEST_CASE("a block PNG replaces exactly its own tile") {
    const BlockRegistry registry = BlockRegistry::create_default();
    const std::uint16_t dirt = registry.id_of("dirt");
    const std::size_t side_tile = opencraft::render::tile_index(dirt, 1);
    const TempDir temp;

    // Top half magenta, bottom half cyan: the orientation is part of the
    // assertion, not just the pixels.
    std::vector<std::uint32_t> pattern(static_cast<std::size_t>(kTileSize) * kTileSize);
    for (int row = 0; row < kTileSize; ++row) {
        for (int col = 0; col < kTileSize; ++col) {
            pattern[static_cast<std::size_t>(row) * kTileSize + col] = row < kTileSize / 2 ? kMagenta : kCyan;
        }
    }
    write_file(opencraft::client::block_tile_path(temp.path(), "dirt", 1), make_png(kTileSize, kTileSize, pattern));

    const AtlasImage atlas = opencraft::client::generate_atlas(registry, temp.path());

    CHECK(tile_differences(atlas, side_tile, atlas_order(pattern)) == 0);
    // Nothing else moved: every other tile still matches the frozen digests.
    CHECK(tiles_off_golden(atlas, side_tile) == 0);

    // Spelled out once, so a flipped decode cannot hide behind the helper:
    // the file's first row is the visual top of the face and therefore lives
    // in the tile's last atlas row.
    const int tx = static_cast<int>(side_tile % 9) * kTileSize;
    const int ty = static_cast<int>(side_tile / 9) * kTileSize;
    CHECK(atlas.pixels[static_cast<std::size_t>(ty + kTileSize - 1) * atlas.width + tx] == kMagenta);
    CHECK(atlas.pixels[static_cast<std::size_t>(ty) * atlas.width + tx] == kCyan);
}

TEST_CASE("each slot takes its own file, in the mesher's slot order") {
    const BlockRegistry registry = BlockRegistry::create_default();
    const std::uint16_t dirt = registry.id_of("dirt");
    const TempDir temp;

    write_file(opencraft::client::block_tile_path(temp.path(), "dirt", 0),
               make_png(kTileSize, kTileSize, solid(kTileSize, kTileSize, kRed)));
    write_file(opencraft::client::block_tile_path(temp.path(), "dirt", 1),
               make_png(kTileSize, kTileSize, solid(kTileSize, kTileSize, kGreen)));
    write_file(opencraft::client::block_tile_path(temp.path(), "dirt", 2),
               make_png(kTileSize, kTileSize, solid(kTileSize, kTileSize, kBlue)));

    const AtlasImage atlas = opencraft::client::generate_atlas(registry, temp.path());

    const std::array<std::uint32_t, opencraft::client::kTilePixelCount> red_tile =
        atlas_order(solid(kTileSize, kTileSize, kRed));
    const std::array<std::uint32_t, opencraft::client::kTilePixelCount> green_tile =
        atlas_order(solid(kTileSize, kTileSize, kGreen));
    const std::array<std::uint32_t, opencraft::client::kTilePixelCount> blue_tile =
        atlas_order(solid(kTileSize, kTileSize, kBlue));

    CHECK(tile_differences(atlas, opencraft::render::tile_index(dirt, 0), red_tile) == 0);
    CHECK(tile_differences(atlas, opencraft::render::tile_index(dirt, 1), green_tile) == 0);
    CHECK(tile_differences(atlas, opencraft::render::tile_index(dirt, 2), blue_tile) == 0);
    CHECK(tiles_off_golden(atlas, opencraft::render::tile_index(dirt, 1)) == 2); // the two we overrode
}

TEST_CASE("only the named block's tiles change") {
    const BlockRegistry registry = BlockRegistry::create_default();
    const std::uint16_t sand = registry.id_of("sand");
    const std::uint16_t stone = registry.id_of("stone");
    const TempDir temp;

    write_file(opencraft::client::block_tile_path(temp.path(), "sand", 1),
               make_png(kTileSize, kTileSize, solid(kTileSize, kTileSize, kMagenta)));

    const AtlasImage atlas = opencraft::client::generate_atlas(registry, temp.path());

    std::size_t off = 0;
    for (std::size_t tile = 0; tile < kGoldenTiles.size(); ++tile) {
        if (tile != opencraft::render::tile_index(sand, 1) && fnv_tile(atlas, tile) != kGoldenTiles[tile]) {
            ++off;
        }
    }
    CHECK(off == 0);
    // The neighbours keep their procedural pixels.
    CHECK(fnv_tile(atlas, opencraft::render::tile_index(sand, 0)) ==
          kGoldenTiles[opencraft::render::tile_index(sand, 0)]);
    CHECK(fnv_tile(atlas, opencraft::render::tile_index(stone, 1)) ==
          kGoldenTiles[opencraft::render::tile_index(stone, 1)]);
}

TEST_CASE("alpha survives the round trip") {
    const BlockRegistry registry = BlockRegistry::create_default();
    const std::uint16_t water = registry.id_of("water");
    const TempDir temp;

    write_file(opencraft::client::block_tile_path(temp.path(), "water", 1),
               make_png(kTileSize, kTileSize, solid(kTileSize, kTileSize, rgba(10, 20, 30, 96))));

    const AtlasImage atlas = opencraft::client::generate_atlas(registry, temp.path());

    const std::size_t tile = opencraft::render::tile_index(water, 1);
    const int tx = static_cast<int>(tile % 9) * kTileSize;
    const int ty = static_cast<int>(tile / 9) * kTileSize;
    CHECK(atlas.pixels[static_cast<std::size_t>(ty) * atlas.width + tx] == rgba(10, 20, 30, 96));
}

// ── the failure modes: warn once, then fall back ────────────────────────────

TEST_CASE("a 32x32 PNG is rejected with a warning and falls back") {
    const BlockRegistry registry = BlockRegistry::create_default();
    const std::uint16_t dirt = registry.id_of("dirt");
    const std::size_t side_tile = opencraft::render::tile_index(dirt, 1);
    const TempDir temp;

    const std::filesystem::path file = opencraft::client::block_tile_path(temp.path(), "dirt", 1);
    write_file(file, make_png(32, 32, solid(32, 32, kMagenta)));

    CaptureWarnings warnings;
    const AtlasImage atlas = opencraft::client::generate_atlas(registry, temp.path());

    CHECK(fnv_tile(atlas, side_tile) == kGoldenTiles[side_tile]);
    CHECK(tiles_off_golden(atlas, kGoldenTiles.size()) == 0);
    CHECK(warnings.count() == 1);
    CHECK(warnings.mentions("32x32"));
    CHECK(warnings.mentions("dirt_side.png"));
}

TEST_CASE("a non-PNG file is rejected with a warning and falls back") {
    const BlockRegistry registry = BlockRegistry::create_default();
    const std::uint16_t dirt = registry.id_of("dirt");
    const TempDir temp;

    const std::string text = "this is not a png, it is a note to the art director\n";
    write_file(opencraft::client::block_tile_path(temp.path(), "dirt", 1),
               std::vector<std::uint8_t>(text.begin(), text.end()));

    CaptureWarnings warnings;
    const AtlasImage atlas = opencraft::client::generate_atlas(registry, temp.path());

    CHECK(tiles_off_golden(atlas, kGoldenTiles.size()) == 0);
    CHECK(warnings.count() == 1);
    CHECK(warnings.mentions("not a PNG"));
}

TEST_CASE("an empty file is rejected with a warning and falls back") {
    const BlockRegistry registry = BlockRegistry::create_default();
    const std::uint16_t dirt = registry.id_of("dirt");
    const TempDir temp;

    write_file(opencraft::client::block_tile_path(temp.path(), "dirt", 1), {});

    CaptureWarnings warnings;
    const AtlasImage atlas = opencraft::client::generate_atlas(registry, temp.path());

    CHECK(tiles_off_golden(atlas, kGoldenTiles.size()) == 0);
    CHECK(warnings.count() == 1);
    CHECK(warnings.mentions("dirt_side.png"));
}

TEST_CASE("a truncated PNG is rejected with a warning and falls back") {
    const BlockRegistry registry = BlockRegistry::create_default();
    const TempDir temp;

    std::vector<std::uint8_t> png = make_png(kTileSize, kTileSize, solid(kTileSize, kTileSize, kMagenta));
    png.resize(png.size() / 2);
    write_file(opencraft::client::block_tile_path(temp.path(), "dirt", 1), png);

    CaptureWarnings warnings;
    const AtlasImage atlas = opencraft::client::generate_atlas(registry, temp.path());

    CHECK(tiles_off_golden(atlas, kGoldenTiles.size()) == 0);
    CHECK(warnings.count() == 1);
}

TEST_CASE("a directory named like a tile is rejected, not read") {
    const BlockRegistry registry = BlockRegistry::create_default();
    const TempDir temp;

    std::error_code ec;
    std::filesystem::create_directories(opencraft::client::block_tile_path(temp.path(), "dirt", 1), ec);
    REQUIRE(!ec);

    CaptureWarnings warnings;
    const AtlasImage atlas = opencraft::client::generate_atlas(registry, temp.path());

    CHECK(tiles_off_golden(atlas, kGoldenTiles.size()) == 0);
    CHECK(warnings.count() == 0); // absent-looking, and "absent" is silent
}

// ── the loader and the root search, on their own ────────────────────────────

TEST_CASE("load_tile_png reports absent vs rejected without throwing") {
    const TempDir temp;
    const std::filesystem::path absent = temp.path() / "blocks" / "nothing_side.png";

    CHECK_FALSE(opencraft::client::load_tile_png(absent).has_value());

    const std::filesystem::path good = temp.path() / "good.png";
    write_file(good, make_png(kTileSize, kTileSize, solid(kTileSize, kTileSize, kMagenta)));
    const auto loaded = opencraft::client::load_tile_png(good);
    REQUIRE(loaded.has_value());
    CHECK(loaded->at(0) == kMagenta);
    CHECK(loaded->at(opencraft::client::kTilePixelCount - 1) == kMagenta);
}

TEST_CASE("the asset root search takes the first existing candidate") {
    const TempDir temp;
    const std::filesystem::path real = temp.path() / "assets";
    std::error_code ec;
    std::filesystem::create_directories(real, ec);
    REQUIRE(!ec);

    const std::filesystem::path missing = temp.path() / "nope";
    CHECK(opencraft::client::resolve_assets_root({missing, real}) == real);
    CHECK(opencraft::client::resolve_assets_root({missing, temp.path() / "also_nope"}).empty());
    CHECK(opencraft::client::resolve_assets_root({}).empty());
}

TEST_CASE("the item icon channel loads from assets/items") {
    const TempDir temp;
    write_file(opencraft::client::item_icon_path(temp.path(), "sunroot"),
               make_png(kTileSize, kTileSize, solid(kTileSize, kTileSize, kGreen)));

    const auto icon = opencraft::client::load_item_icon(temp.path(), "sunroot");
    REQUIRE(icon.has_value());
    CHECK(icon->at(0) == kGreen);

    // The channel is path-only for now: an item with no art is simply absent.
    CHECK_FALSE(opencraft::client::load_item_icon(temp.path(), "cave_cap").has_value());
    // A block texture is not an item icon and vice versa.
    write_file(opencraft::client::block_tile_path(temp.path(), "dirt", 1),
               make_png(kTileSize, kTileSize, solid(kTileSize, kTileSize, kRed)));
    CHECK_FALSE(opencraft::client::load_item_icon(temp.path(), "dirt").has_value());
}
