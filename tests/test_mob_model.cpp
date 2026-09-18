// T-B1: the mob model channel (docs/tasks/T-B1.md, docs/research/12-mob-model-
// formats.md).
//
// Three things are asserted here, in the order the card lists them:
//   1. the decoder reads our own B0 fixture (tests/fixtures/mobs/) exactly, and
//      the "+1 palette index" trap of the format is pinned to the raw bytes by
//      this file's own chunk walker rather than by the parser it tests;
//   2. the fallback table (§4 / research §6.5) is complete: seven states, one
//      WARN where the table says one and silence where it says silence;
//   3. the mesher and the pose module behave as the contracts say - culled
//      faces only, CCW-from-outside winding, joints contiguous, and yaw/pitch
//      copied out of the simulation untouched.
//
// The fixtures are ours (make_fixtures.py next to them spells out every byte);
// no MagicaVoxel sample is in the repository.

#include <doctest/doctest.h>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "mob_mesh.hpp"
#include "mob_model.hpp"
#include "mob_pose.hpp"

namespace {

using opencraft::client::build_mob_mesh;
using opencraft::client::kMobJointCount;
using opencraft::client::kMobPaletteSize;
using opencraft::client::mob_joint_of_color;
using opencraft::client::mob_pose;
using opencraft::client::MobAnimClock;
using opencraft::client::MobJoint;
using opencraft::client::MobMesh;
using opencraft::client::MobPose;
using opencraft::client::MobPoseInput;
using opencraft::client::parse_vox;
using opencraft::client::VoxError;
using opencraft::client::VoxModel;
using opencraft::client::VoxVoxel;

// The fixtures are the same bytes the repo ships; a test that cannot find them
// must fail loudly rather than silently assert nothing.
std::filesystem::path fixture_dir() {
    return std::filesystem::path(OPENCRAFT_FIXTURE_DIR) / "mobs";
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "fixture missing: " << path.string());
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> column_fixture() {
    return read_bytes(fixture_dir() / "mob_column.vox");
}

constexpr std::uint32_t rgba(int r, int g, int b, int a = 255) {
    return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(r);
}

// ── an independent reader for the bits the test needs to pin ────────────────
//
// Deliberately NOT written in terms of parse_vox(): the palette-index test is
// only worth something if the expected value comes from the file's own bytes.
struct RawChunk {
    std::string id;
    std::size_t content = 0;
    std::size_t content_size = 0;
};

std::vector<RawChunk> raw_chunks(const std::vector<std::uint8_t> &bytes) {
    std::vector<RawChunk> chunks;
    std::size_t offset = 8; // "VOX " + version
    while (offset + 12 <= bytes.size()) {
        const auto size = static_cast<std::size_t>(static_cast<std::uint32_t>(bytes[offset + 4]) |
                                                   (static_cast<std::uint32_t>(bytes[offset + 5]) << 8) |
                                                   (static_cast<std::uint32_t>(bytes[offset + 6]) << 16) |
                                                   (static_cast<std::uint32_t>(bytes[offset + 7]) << 24));
        chunks.push_back(RawChunk{std::string(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                              bytes.begin() + static_cast<std::ptrdiff_t>(offset) + 4),
                                  offset + 12, size});
        offset += 12 + size;
    }
    return chunks;
}

const RawChunk *find_raw(const std::vector<RawChunk> &chunks, const std::string &id) {
    for (const RawChunk &chunk : chunks) {
        if (chunk.id == id) {
            return &chunk;
        }
    }
    return nullptr;
}

// ── a scratch asset tree ────────────────────────────────────────────────────

class TempDir {
public:
    TempDir() {
        static int counter = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("opencraft_tb1_" + std::to_string(stamp) + "_" + std::to_string(counter++));
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

// Copies one of our fixtures in under a mob id, the way an artist would.
void place_model(const std::filesystem::path &root, const std::string &mob_id, const std::string &fixture) {
    write_file(opencraft::client::mob_model_path(root, mob_id), read_bytes(fixture_dir() / fixture));
}

// ── log capture (the T-A2 pattern: "one WARN" is an assertion, not a hope) ──

class CaptureLog {
public:
    CaptureLog() : logger_(spdlog::default_logger()), previous_level_(logger_->level()) {
        logger_->set_level(spdlog::level::info);
        logger_->sinks().push_back(sink_);
    }

    ~CaptureLog() {
        auto &sinks = logger_->sinks();
        const auto it = std::find_if(sinks.begin(), sinks.end(), [this](const std::shared_ptr<spdlog::sinks::sink> &s) {
            return s.get() == sink_.get();
        });
        if (it != sinks.end()) {
            sinks.erase(it);
        }
        logger_->set_level(previous_level_);
    }

    CaptureLog(const CaptureLog &) = delete;
    CaptureLog &operator=(const CaptureLog &) = delete;

    [[nodiscard]] const std::vector<std::string> &messages() const { return sink_->messages(); }

    [[nodiscard]] std::size_t count() const { return sink_->messages().size(); }

    [[nodiscard]] bool mentions(const std::string &needle) const {
        const std::vector<std::string> &all = sink_->messages();
        return std::any_of(all.begin(), all.end(),
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

// ── the fixture's contents, spelled out (B0: "体素数据可逐字列出") ───────────

const std::vector<VoxVoxel> kColumnVoxels{
    {8, 8, 0, 5}, {8, 8, 1, 5}, {8, 8, 2, 1}, {8, 8, 3, 1}, {8, 8, 4, 1}, {8, 8, 5, 1}, {8, 8, 6, 2}, {8, 8, 7, 2},
};

} // namespace

// ════════════════════════════════════════════════════════════════════════════
// 1. the decoder against our own fixture
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("T-B1 vox: the committed fixture parses to the documented model") {
    const std::vector<std::uint8_t> bytes = column_fixture();
    VoxError error = VoxError::NotVoxFile;
    const std::optional<VoxModel> model = parse_vox(bytes.data(), bytes.size(), error);

    REQUIRE(model.has_value());
    CHECK(error == VoxError::None);
    CHECK(model->size_x == 16);
    CHECK(model->size_y == 16);
    CHECK(model->size_z == 16);

    REQUIRE(model->voxels.size() == kColumnVoxels.size());
    for (std::size_t i = 0; i < kColumnVoxels.size(); ++i) {
        CHECK(static_cast<int>(model->voxels[i].x) == kColumnVoxels[i].x);
        CHECK(static_cast<int>(model->voxels[i].y) == kColumnVoxels[i].y);
        CHECK(static_cast<int>(model->voxels[i].z) == kColumnVoxels[i].z);
        CHECK(static_cast<int>(model->voxels[i].color) == kColumnVoxels[i].color);
    }
}

TEST_CASE("T-B1 vox: colorIndex N reads palette entry N-1, pinned to the raw bytes") {
    const std::vector<std::uint8_t> bytes = column_fixture();
    const std::vector<RawChunk> chunks = raw_chunks(bytes);
    const RawChunk *rgba_chunk = find_raw(chunks, "RGBA");
    REQUIRE(rgba_chunk != nullptr);
    REQUIRE(rgba_chunk->content_size >= kMobPaletteSize * 4);

    // What the FILE says the colors are, read here without the parser.
    std::array<std::uint32_t, kMobPaletteSize> raw_palette{};
    for (std::size_t entry = 0; entry < kMobPaletteSize; ++entry) {
        const std::size_t at = rgba_chunk->content + entry * 4;
        raw_palette[entry + 1 < kMobPaletteSize ? entry + 1 : 0] =
            rgba(bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]);
    }

    VoxError error = VoxError::None;
    const std::optional<VoxModel> model = parse_vox(bytes.data(), bytes.size(), error);
    REQUIRE(model.has_value());

    // Entry e of the chunk is colorIndex e+1 - the off-by-one the format's own
    // documentation warns about, and the reason index 0 is never painted.
    CHECK(model->palette[0] == 0U);
    CHECK(model->palette[1] == rgba(180, 120, 70)); // chunk entry 0
    CHECK(model->palette[2] == rgba(90, 170, 90));  // chunk entry 1
    CHECK(model->palette[5] == rgba(170, 170, 60)); // chunk entry 4: the joint-5 label
    CHECK(model->palette[8] == rgba(240, 240, 240));
    CHECK(model->palette[9] == 0U); // never authored
    for (std::size_t color = 1; color < kMobPaletteSize; ++color) {
        CHECK(model->palette[color] == raw_palette[color]);
    }
}

TEST_CASE("T-B1 vox: a valid file with zero voxels parses, and is not an error") {
    const std::vector<std::uint8_t> bytes = read_bytes(fixture_dir() / "mob_empty.vox");
    VoxError error = VoxError::NotVoxFile;
    const std::optional<VoxModel> model = parse_vox(bytes.data(), bytes.size(), error);

    // "Empty" is a content mistake the LOADER reports (below); the decoder's
    // job is to say what the file says.
    REQUIRE(model.has_value());
    CHECK(error == VoxError::None);
    CHECK(model->voxels.empty());
    CHECK(model->size_x == 16);
    CHECK(model->palette[1] == rgba(180, 120, 70));
}

TEST_CASE("T-B1 vox: bad input is a refusal, never a crash") {
    const std::vector<std::uint8_t> good = column_fixture();

    SUBCASE("null and short buffers") {
        VoxError error = VoxError::None;
        CHECK_FALSE(parse_vox(nullptr, 0, error).has_value());
        CHECK(error == VoxError::NotVoxFile);
        CHECK_FALSE(parse_vox(good.data(), 4, error).has_value());
        CHECK(error == VoxError::NotVoxFile);
    }

    SUBCASE("wrong signature") {
        std::vector<std::uint8_t> bytes = good;
        bytes[0] = 'P'; // "POV "
        VoxError error = VoxError::None;
        CHECK_FALSE(parse_vox(bytes.data(), bytes.size(), error).has_value());
        CHECK(error == VoxError::NotVoxFile);
    }

    SUBCASE("truncated in the middle of a chunk") {
        std::vector<std::uint8_t> bytes = good;
        bytes.resize(bytes.size() - 100); // cuts into the RGBA content
        VoxError error = VoxError::None;
        CHECK_FALSE(parse_vox(bytes.data(), bytes.size(), error).has_value());
        CHECK(error == VoxError::Truncated);
    }

    SUBCASE("XYZI claims more voxels than it holds") {
        std::vector<std::uint8_t> bytes = good;
        const std::vector<RawChunk> chunks = raw_chunks(bytes);
        const RawChunk *xyzi = find_raw(chunks, "XYZI");
        REQUIRE(xyzi != nullptr);
        bytes[xyzi->content] = 200; // numVoxels: 8 -> 200
        VoxError error = VoxError::None;
        CHECK_FALSE(parse_vox(bytes.data(), bytes.size(), error).has_value());
        CHECK(error == VoxError::VoxelCountLies);
    }

    SUBCASE("a voxel coordinate outside SIZE") {
        std::vector<std::uint8_t> bytes = good;
        const std::vector<RawChunk> chunks = raw_chunks(bytes);
        const RawChunk *xyzi = find_raw(chunks, "XYZI");
        REQUIRE(xyzi != nullptr);
        bytes[xyzi->content + 4] = 200; // first voxel's x
        VoxError error = VoxError::None;
        CHECK_FALSE(parse_vox(bytes.data(), bytes.size(), error).has_value());
        CHECK(error == VoxError::VoxelOutOfRange);
    }

    SUBCASE("SIZE of zero") {
        std::vector<std::uint8_t> bytes = good;
        const std::vector<RawChunk> chunks = raw_chunks(bytes);
        const RawChunk *size = find_raw(chunks, "SIZE");
        REQUIRE(size != nullptr);
        bytes[size->content + 8] = 0; // z extent: 16 -> 0
        VoxError error = VoxError::None;
        CHECK_FALSE(parse_vox(bytes.data(), bytes.size(), error).has_value());
        CHECK(error == VoxError::BadGridSize);
    }
}

TEST_CASE("T-B1 vox: chunks we do not draw are stepped over, and PACK keeps the first model") {
    // A file built here rather than read, so the shape under test is visible:
    // an unknown chunk, a PACK (the multi-model layout we deliberately do not
    // support), and a second SIZE/XYZI pair after the first.
    const auto i32 = [](int value) {
        std::vector<std::uint8_t> out(4);
        for (int i = 0; i < 4; ++i) {
            out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
        }
        return out;
    };
    const auto chunk = [&](const char *id, const std::vector<std::uint8_t> &content) {
        std::vector<std::uint8_t> out(id, id + 4);
        const std::vector<std::uint8_t> size = i32(static_cast<int>(content.size()));
        out.insert(out.end(), size.begin(), size.end());
        out.insert(out.end(), {0, 0, 0, 0}); // no children
        out.insert(out.end(), content.begin(), content.end());
        return out;
    };
    const auto size_chunk = [&](int side) {
        std::vector<std::uint8_t> content = i32(side);
        const std::vector<std::uint8_t> axis = i32(side);
        content.insert(content.end(), axis.begin(), axis.end());
        content.insert(content.end(), axis.begin(), axis.end());
        return chunk("SIZE", content);
    };
    const auto voxel_chunk = [&](int count) {
        std::vector<std::uint8_t> content = i32(count);
        for (int i = 0; i < count; ++i) {
            content.insert(content.end(), {0, 0, static_cast<std::uint8_t>(i), 1});
        }
        return chunk("XYZI", content);
    };

    std::vector<std::uint8_t> body = chunk("MATT", {1, 2, 3, 4});
    const std::vector<std::uint8_t> pack = chunk("PACK", i32(2));
    body.insert(body.begin(), pack.begin(), pack.end());
    const std::vector<std::uint8_t> first_size = size_chunk(4);
    body.insert(body.end(), first_size.begin(), first_size.end());
    const std::vector<std::uint8_t> first_voxels = voxel_chunk(2);
    body.insert(body.end(), first_voxels.begin(), first_voxels.end());
    // The second model: same SIZE, three voxels. It must be ignored.
    const std::vector<std::uint8_t> second_size = size_chunk(4);
    body.insert(body.end(), second_size.begin(), second_size.end());
    const std::vector<std::uint8_t> second_voxels = voxel_chunk(3);
    body.insert(body.end(), second_voxels.begin(), second_voxels.end());
    std::vector<std::uint8_t> rgba_content;
    for (int i = 0; i < 256; ++i) {
        rgba_content.insert(rgba_content.end(), {10, 20, 30, 255});
    }
    const std::vector<std::uint8_t> rgba_chunk = chunk("RGBA", rgba_content);
    body.insert(body.end(), rgba_chunk.begin(), rgba_chunk.end());

    std::vector<std::uint8_t> bytes = {'V', 'O', 'X', ' ', 150, 0, 0, 0};
    bytes.insert(bytes.end(), body.begin(), body.end());

    VoxError error = VoxError::None;
    const std::optional<VoxModel> model = parse_vox(bytes.data(), bytes.size(), error);
    REQUIRE(model.has_value());
    CHECK(error == VoxError::None);
    CHECK(model->size_x == 4);
    CHECK(model->voxels.size() == 2); // the first model's, not the second's
    CHECK(model->palette[1] == rgba(10, 20, 30));
}

TEST_CASE("T-B1 vox: a palette that holds only the entries the model uses is accepted") {
    // The format says the RGBA chunk is 256 entries, but a minimal writer (a
    // hand-authored model, a small generator) reasonably emits only the colors
    // it uses - and the PM's independent verification kit does exactly that
    // (docs/qa/T-B1-2026-09-18/pm_verify/, fixture pm_good.vox: 3 entries).
    // Rejecting those would have failed that kit's very first row, so the
    // decoder reads what is there and leaves the rest zeroed ("unused", which
    // is what a zero paletted entry already means).
    const auto i32 = [](int value) {
        std::vector<std::uint8_t> out(4);
        for (int i = 0; i < 4; ++i) {
            out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
        }
        return out;
    };
    const auto chunk = [&](const char *id, const std::vector<std::uint8_t> &content) {
        std::vector<std::uint8_t> out(id, id + 4);
        const std::vector<std::uint8_t> size = i32(static_cast<int>(content.size()));
        out.insert(out.end(), size.begin(), size.end());
        out.insert(out.end(), {0, 0, 0, 0});
        out.insert(out.end(), content.begin(), content.end());
        return out;
    };
    const auto rgba_chunk = [&](const std::vector<std::uint32_t> &colors) {
        std::vector<std::uint8_t> content;
        for (std::uint32_t packed : colors) {
            content.push_back(static_cast<std::uint8_t>(packed & 0xFFU));
            content.push_back(static_cast<std::uint8_t>((packed >> 8) & 0xFFU));
            content.push_back(static_cast<std::uint8_t>((packed >> 16) & 0xFFU));
            content.push_back(static_cast<std::uint8_t>((packed >> 24) & 0xFFU));
        }
        return chunk("RGBA", content);
    };

    std::vector<std::uint8_t> body = chunk("SIZE", [&] {
        // One named copy of the axis value: `i32(4).begin(), i32(4).end()`
        // would iterate two DIFFERENT temporaries.
        const std::vector<std::uint8_t> axis = i32(4);
        std::vector<std::uint8_t> size = axis;
        size.insert(size.end(), axis.begin(), axis.end());
        size.insert(size.end(), axis.begin(), axis.end());
        return size;
    }());
    std::vector<std::uint8_t> voxels_data = i32(1);
    voxels_data.insert(voxels_data.end(), {1, 1, 1, 2});
    const std::vector<std::uint8_t> voxels = chunk("XYZI", voxels_data);
    body.insert(body.end(), voxels.begin(), voxels.end());
    // Entry 0 of the chunk is colorIndex 1: three entries, three colors.
    const std::vector<std::uint8_t> palette_chunk = rgba_chunk({rgba(255, 0, 0), rgba(0, 255, 0), rgba(0, 0, 255)});
    body.insert(body.end(), palette_chunk.begin(), palette_chunk.end());

    std::vector<std::uint8_t> bytes = {'V', 'O', 'X', ' ', 150, 0, 0, 0};
    bytes.insert(bytes.end(), body.begin(), body.end());

    VoxError error = VoxError::NotVoxFile;
    const std::optional<VoxModel> model = parse_vox(bytes.data(), bytes.size(), error);
    REQUIRE(model.has_value());
    CHECK(error == VoxError::None);
    CHECK(model->voxels.size() == 1);
    CHECK(model->palette[1] == rgba(255, 0, 0));
    CHECK(model->palette[2] == rgba(0, 255, 0));
    CHECK(model->palette[3] == rgba(0, 0, 255));
    CHECK(model->palette[4] == 0U);   // not in the file: unused
    CHECK(model->palette[255] == 0U); // same

    SUBCASE("a zero-entry palette chunk is still a refusal") {
        std::vector<std::uint8_t> empty = bytes;
        // Drop the 12-byte RGBA content and declare a length of 0. The chunk is
        // the last one in this file, so its 12-byte header starts 24 bytes from
        // the end (12 header + 12 content).
        const std::size_t at = empty.size() - 24;
        CHECK(std::string(empty.begin() + static_cast<std::ptrdiff_t>(at),
                          empty.begin() + static_cast<std::ptrdiff_t>(at) + 4) == "RGBA");
        empty.resize(empty.size() - 12);
        std::fill(empty.begin() + static_cast<std::ptrdiff_t>(at) + 4,
                  empty.begin() + static_cast<std::ptrdiff_t>(at) + 8, 0);
        VoxError sub_error = VoxError::None;
        CHECK_FALSE(parse_vox(empty.data(), empty.size(), sub_error).has_value());
        CHECK(sub_error == VoxError::BadChunk);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// 2. the fallback table (卡 §4 / research §6.5) - one SUBCASE per row
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("T-B1 loader: with no model file the mob keeps its boxes, silently") {
    const std::vector<std::string> roster{"mossback", "hollow_wretch", "blastbud"};

    SUBCASE("no asset tree at all") {
        CaptureLog log;
        const std::vector<opencraft::client::MobModel> loaded =
            opencraft::client::load_mob_models(std::filesystem::path{}, roster);
        CHECK(loaded.empty());
        CHECK(log.count() == 0); // not even the count line: there is no root to name
    }

    SUBCASE("an asset root with no mobs/ directory") {
        TempDir temp;
        CaptureLog log;
        const std::vector<opencraft::client::MobModel> loaded = opencraft::client::load_mob_models(temp.path(), roster);
        CHECK(loaded.empty());
        // The startup count is still printed (it is the machine criterion), but
        // there is no warning: a missing directory is not a mistake.
        REQUIRE(log.count() == 1);
        CHECK(log.mentions("mobs: 0/3 mob models loaded from"));
        CHECK(log.mentions("/mobs"));
    }

    SUBCASE("a mobs/ directory with no file for this mob") {
        TempDir temp;
        std::error_code ec;
        std::filesystem::create_directories(temp.path() / "mobs", ec);
        CaptureLog log;
        const std::vector<opencraft::client::MobModel> loaded = opencraft::client::load_mob_models(temp.path(), roster);
        CHECK(loaded.empty());
        REQUIRE(log.count() == 1);
        CHECK(log.mentions("mobs: 0/3 mob models loaded from"));
    }
}

TEST_CASE("T-B1 loader: a present file that cannot be used is exactly one warning") {
    const std::vector<std::string> roster{"mossback"};

    SUBCASE("not a vox file") {
        TempDir temp;
        write_file(opencraft::client::mob_model_path(temp.path(), "mossback"), {1, 2, 3, 4, 5, 6, 7, 8});
        CaptureLog log;
        CHECK(opencraft::client::load_mob_models(temp.path(), roster).empty());
        REQUIRE(log.count() == 2); // the WARN plus the count line
        CHECK(log.mentions("missing the VOX signature"));
        CHECK(log.mentions("mobs: 0/1 mob models loaded from"));
    }

    SUBCASE("truncated") {
        TempDir temp;
        std::vector<std::uint8_t> bytes = column_fixture();
        bytes.resize(bytes.size() - 100);
        write_file(opencraft::client::mob_model_path(temp.path(), "mossback"), bytes);
        CaptureLog log;
        CHECK(opencraft::client::load_mob_models(temp.path(), roster).empty());
        REQUIRE(log.count() == 2);
        CHECK(log.mentions("a chunk runs past the end of the file"));
    }

    SUBCASE("parses, but holds no voxels") {
        TempDir temp;
        place_model(temp.path(), "mossback", "mob_empty.vox");
        CaptureLog log;
        CHECK(opencraft::client::load_mob_models(temp.path(), roster).empty());
        REQUIRE(log.count() == 2);
        CHECK(log.mentions("holds 0 voxels"));
    }
}

TEST_CASE("T-B1 loader: the palette PNG overrides the built-in palette, or falls back") {
    const std::vector<std::string> roster{"mossback"};

    SUBCASE("no palette PNG: the file's own palette, silent") {
        TempDir temp;
        place_model(temp.path(), "mossback", "mob_column.vox");
        CaptureLog log;
        const std::vector<opencraft::client::MobModel> loaded = opencraft::client::load_mob_models(temp.path(), roster);
        REQUIRE(loaded.size() == 1);
        CHECK_FALSE(loaded.front().palette_from_png);
        CHECK(loaded.front().palette[1] == rgba(180, 120, 70));
        CHECK(log.mentions("mobs: 1/1 mob models loaded from"));
        CHECK_FALSE(log.mentions("is not a PNG"));
    }

    SUBCASE("a broken palette PNG: the file's own palette, one warning") {
        TempDir temp;
        place_model(temp.path(), "mossback", "mob_column.vox");
        write_file(opencraft::client::mob_palette_path(temp.path(), "mossback"), {0xDE, 0xAD, 0xBE, 0xEF});
        CaptureLog log;
        const std::vector<opencraft::client::MobModel> loaded = opencraft::client::load_mob_models(temp.path(), roster);
        REQUIRE(loaded.size() == 1);
        CHECK_FALSE(loaded.front().palette_from_png);
        CHECK(loaded.front().palette[1] == rgba(180, 120, 70)); // still draws
        CHECK(log.mentions("is not a PNG"));
        CHECK(log.mentions("mobs: 1/1 mob models loaded from"));
    }

    SUBCASE("a good palette PNG wins, entry for entry") {
        TempDir temp;
        place_model(temp.path(), "mossback", "mob_column.vox");
        std::error_code ec;
        std::filesystem::create_directories(temp.path() / "palettes", ec);
        std::filesystem::copy_file(fixture_dir() / "mob_column_palette.png",
                                   opencraft::client::mob_palette_path(temp.path(), "mossback"), ec);
        REQUIRE_FALSE(ec);
        const std::vector<opencraft::client::MobModel> loaded = opencraft::client::load_mob_models(temp.path(), roster);
        REQUIRE(loaded.size() == 1);
        CHECK(loaded.front().palette_from_png);
        // make_fixtures.py paints the PNG palette with the opposite colors on
        // purpose: a test can then tell which palette is live from one entry.
        // The PNG's cell number IS the colorIndex (no "+1" shift here - cell 0
        // is the unused index 0). tests/fixtures/mobs/make_fixtures.py spells
        // the same contract out on the authoring side.
        CHECK(loaded.front().palette[1] == rgba(255, 0, 255));
        CHECK(loaded.front().palette[2] == rgba(0, 255, 255));
        CHECK(loaded.front().palette[5] == rgba(255, 255, 0));
        CHECK(loaded.front().palette[8] == rgba(32, 32, 32));
        CHECK(loaded.front().palette[0] == 0U); // never painted, always transparent
    }

    SUBCASE("the count line counts the roster, not the files") {
        TempDir temp;
        place_model(temp.path(), "mossback", "mob_column.vox");
        CaptureLog log;
        const std::vector<opencraft::client::MobModel> loaded = opencraft::client::load_mob_models(
            temp.path(), std::vector<std::string>{"mossback", "hollow_wretch", "blastbud"});
        CHECK(loaded.size() == 1);
        CHECK(log.mentions("mobs: 1/3 mob models loaded from"));
    }
}

// ════════════════════════════════════════════════════════════════════════════
// 3. the mesher
// ════════════════════════════════════════════════════════════════════════════

namespace {

VoxModel one_voxel_model(std::uint8_t color) {
    VoxModel model;
    model.size_x = 4;
    model.size_y = 4;
    model.size_z = 4;
    model.voxels.push_back(VoxVoxel{1, 1, 1, color});
    model.palette[color] = rgba(200, 100, 50);
    return model;
}

VoxModel two_voxel_model(std::uint8_t color) {
    VoxModel model = one_voxel_model(color);
    model.voxels.push_back(VoxVoxel{2, 1, 1, color});
    return model;
}

// Every triangle's geometric normal must point away from the mesh's centre:
// that is what "CCW seen from outside" means, and it is the property the mob
// pass relies on when it leaves GL_CULL_FACE on.
std::size_t inward_facing_triangles(const MobMesh &mesh) {
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(-std::numeric_limits<float>::max());
    for (const opencraft::client::MobVertex &v : mesh.vertices) {
        lo = glm::min(lo, glm::vec3(v.x, v.y, v.z));
        hi = glm::max(hi, glm::vec3(v.x, v.y, v.z));
    }
    const glm::vec3 centre = (lo + hi) * 0.5f;
    std::size_t inward = 0;
    for (std::size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
        const glm::vec3 a(mesh.vertices[i].x, mesh.vertices[i].y, mesh.vertices[i].z);
        const glm::vec3 b(mesh.vertices[i + 1].x, mesh.vertices[i + 1].y, mesh.vertices[i + 1].z);
        const glm::vec3 c(mesh.vertices[i + 2].x, mesh.vertices[i + 2].y, mesh.vertices[i + 2].z);
        const glm::vec3 normal = glm::cross(b - a, c - a);
        const glm::vec3 outward = ((a + b + c) / 3.0f) - centre;
        if (glm::dot(normal, outward) <= 0.0f) {
            ++inward;
        }
    }
    return inward;
}

} // namespace

TEST_CASE("T-B1 mesh: culling keeps exactly the faces with nothing behind them") {
    SUBCASE("one voxel is six faces") {
        const VoxModel model = one_voxel_model(1);
        const MobMesh mesh = build_mob_mesh(model, 1.0f);
        CHECK(mesh.vertices.size() == 36);
        CHECK(mesh.parts.size() == 1);
        CHECK(mesh.parts.front().joint == 0);
        CHECK(mesh.parts.front().count == 36);
        CHECK(inward_facing_triangles(mesh) == 0);
    }

    SUBCASE("two touching voxels hide the face between them") {
        const VoxModel model = two_voxel_model(1);
        const MobMesh mesh = build_mob_mesh(model, 1.0f);
        CHECK(mesh.vertices.size() == 60); // 10 faces, not 12
        CHECK(inward_facing_triangles(mesh) == 0);
    }

    SUBCASE("the fixture column is 34 faces, sliced into three joints") {
        VoxError error = VoxError::None;
        const std::vector<std::uint8_t> bytes = column_fixture();
        const std::optional<VoxModel> model = parse_vox(bytes.data(), bytes.size(), error);
        REQUIRE(model.has_value());

        // 8 voxels x 4 sides, plus the two end caps: culling only ever removes
        // the 7 faces that sit between two voxels of the column.
        const MobMesh mesh = build_mob_mesh(*model, 16.0f);
        CHECK(mesh.vertices.size() == 34 * 6);
        CHECK(inward_facing_triangles(mesh) == 0);

        REQUIRE(mesh.parts.size() == 3);
        // Joints ascend, and their ranges tile the buffer exactly once.
        CHECK(static_cast<int>(mesh.parts[0].joint) == MobJoint::kMobJointBody);
        CHECK(static_cast<int>(mesh.parts[1].joint) == MobJoint::kMobJointHead);
        CHECK(static_cast<int>(mesh.parts[2].joint) == MobJoint::kMobJointLegLeft);
        CHECK(mesh.parts[0].first == 0);
        CHECK(mesh.parts[0].count == 16 * 6); // joints 1 (body): z = 2..5
        CHECK(mesh.parts[1].first == 16 * 6);
        CHECK(mesh.parts[1].count == 9 * 6); // joint 2 (head): z = 6,7 plus the cap
        CHECK(mesh.parts[2].first == 25 * 6);
        CHECK(mesh.parts[2].count == 9 * 6); // joint 5 (leg): z = 0,1 plus the cap
        std::size_t total = 0;
        for (const auto &part : mesh.parts) {
            total += part.count;
        }
        CHECK(total == mesh.vertices.size());
    }
}

TEST_CASE("T-B1 mesh: size comes from the mob's own collision height, anchored at the feet") {
    VoxError error = VoxError::None;
    const std::vector<std::uint8_t> bytes = column_fixture();
    const std::optional<VoxModel> model = parse_vox(bytes.data(), bytes.size(), error);
    REQUIRE(model.has_value());

    const MobMesh mesh = build_mob_mesh(*model, 16.0f);
    CHECK(mesh.voxel_height == doctest::Approx(8.0f));
    CHECK(mesh.scale == doctest::Approx(2.0f)); // 16 blocks / 8 voxels

    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(-std::numeric_limits<float>::max());
    for (const opencraft::client::MobVertex &v : mesh.vertices) {
        lo = glm::min(lo, glm::vec3(v.x, v.y, v.z));
        hi = glm::max(hi, glm::vec3(v.x, v.y, v.z));
    }
    CHECK(lo.y == doctest::Approx(0.0f));  // the feet are the entity's position
    CHECK(hi.y == doctest::Approx(16.0f)); // and the head reaches the collision height
    // Centred on the vertical axis, so rotate_y(yaw) spins the mob about its
    // own centre rather than about a corner.
    CHECK((lo.x + hi.x) * 0.5f == doctest::Approx(0.0f));
    CHECK((lo.z + hi.z) * 0.5f == doctest::Approx(0.0f));

    SUBCASE("a shorter mob scales the same model down, uniformly") {
        const MobMesh small = build_mob_mesh(*model, 4.0f);
        CHECK(small.scale == doctest::Approx(0.5f));
        CHECK(small.vertices.size() == mesh.vertices.size()); // geometry is unchanged
    }
}

TEST_CASE("T-B1 mesh: joint pivots follow the documented rule") {
    VoxError error = VoxError::None;
    const std::vector<std::uint8_t> bytes = column_fixture();
    const std::optional<VoxModel> model = parse_vox(bytes.data(), bytes.size(), error);
    REQUIRE(model.has_value());
    const MobMesh mesh = build_mob_mesh(*model, 16.0f);

    // Legs (joint 4, voxels z = 0,1) hang from their TOP: y = 2 voxels * 2.
    CHECK(mesh.pivots[MobJoint::kMobJointLegLeft].y == doctest::Approx(4.0f));
    // Head (joint 1, voxels z = 6,7) tilts about its BOTTOM: y = 6 voxels * 2.
    CHECK(mesh.pivots[MobJoint::kMobJointHead].y == doctest::Approx(12.0f));
    // Body (joint 0, voxels z = 2..5) about its centre: y = 4 voxels * 2.
    CHECK(mesh.pivots[MobJoint::kMobJointBody].y == doctest::Approx(8.0f));
    // The fixture is a single column at x = y = 8, so every pivot is on the axis.
    for (std::size_t joint = 0; joint < kMobJointCount; ++joint) {
        CHECK(mesh.pivots[joint].x == doctest::Approx(0.0f));
        CHECK(mesh.pivots[joint].z == doctest::Approx(0.0f));
    }
}

TEST_CASE("T-B1 mesh: joint labels are the palette's first eight indices") {
    CHECK(mob_joint_of_color(1) == MobJoint::kMobJointBody);
    CHECK(mob_joint_of_color(2) == MobJoint::kMobJointHead);
    CHECK(mob_joint_of_color(3) == MobJoint::kMobJointArmLeft);
    CHECK(mob_joint_of_color(4) == MobJoint::kMobJointArmRight);
    CHECK(mob_joint_of_color(5) == MobJoint::kMobJointLegLeft);
    CHECK(mob_joint_of_color(6) == MobJoint::kMobJointLegRight);
    CHECK(mob_joint_of_color(7) == MobJoint::kMobJointTail);
    CHECK(mob_joint_of_color(8) == MobJoint::kMobJointSpare);
    // Unlabelled colours belong to the body (research/12 §4.3: 9..255 are
    // colour slots).
    CHECK(mob_joint_of_color(0) == MobJoint::kMobJointBody);
    CHECK(mob_joint_of_color(9) == MobJoint::kMobJointBody);
    CHECK(mob_joint_of_color(255) == MobJoint::kMobJointBody);
}

TEST_CASE("T-B1 mesh: no voxels means no geometry, not a crash") {
    const VoxModel empty;
    const MobMesh mesh = build_mob_mesh(empty, 1.4f);
    CHECK(mesh.vertices.empty());
    CHECK(mesh.parts.empty());
}

TEST_CASE("T-B1 mesh: every vertex samples the palette cell that holds its colour") {
    VoxModel model;
    model.size_x = 4;
    model.size_y = 4;
    model.size_z = 4;
    model.voxels.push_back(VoxVoxel{1, 1, 0, 3}); // joint 2 (arm_l), colour 3
    model.voxels.push_back(VoxVoxel{1, 1, 1, 9}); // unlabelled -> body, colour 9
    // Distinct colours per entry: "sampled the wrong cell" must not be able to
    // pass by accident. rgba() puts the entry number in the red byte.
    for (std::size_t color = 1; color < kMobPaletteSize; ++color) {
        model.palette[color] = rgba(static_cast<int>(color), 100, 200);
    }
    const MobMesh mesh = build_mob_mesh(model, 2.0f);

    // The texture layout, spelled out here rather than re-deriving the mesher's
    // formula (a test that restates the code it tests cannot fail): the palette
    // array is uploaded as a 16x16 texture, so texel (col, row) holds
    // palette[row*16 + col] and its centre is at ((col+0.5)/16, (row+0.5)/16).
    const auto sample = [&](float u, float v) {
        const int col = static_cast<int>(u * 16.0f);
        const int row = static_cast<int>(v * 16.0f);
        return static_cast<std::uint32_t>(
            model.palette[static_cast<std::size_t>(row) * 16 + static_cast<std::size_t>(col)]);
    };
    const auto red_of = [](std::uint32_t packed) { return static_cast<int>(packed & 0xFFU); };

    REQUIRE(mesh.parts.size() == 2);
    for (const auto &part : mesh.parts) {
        const std::uint8_t expected_color = part.joint == MobJoint::kMobJointArmLeft ? 3 : 9;
        for (std::uint32_t i = part.first; i < part.first + part.count; ++i) {
            const std::uint32_t sampled = sample(mesh.vertices[i].u, mesh.vertices[i].v);
            // What is sampled IS this colour's entry (not a neighbour's), and it
            // is the colour the voxel was painted with.
            CHECK(sampled == model.palette[expected_color]);
            CHECK(red_of(sampled) == static_cast<int>(expected_color));
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// 4. the pose: simulation in, shape out (research/12 §4.4)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("T-B1 pose: facing is copied from the simulation, never invented") {
    MobPoseInput in;
    in.yaw = 2.5;
    in.pitch = -0.75;
    in.speed = 0.12;
    in.on_ground = true;
    in.hurt_cooldown = 7;
    in.fuse = 12;
    in.baby = true;
    const MobPose pose = mob_pose(in, 1.3f);

    // Exactly the simulation's numbers - no smoothing, no re-derivation, and in
    // particular no "turn to face the player" invented by the renderer.
    CHECK(pose.yaw == static_cast<float>(in.yaw));
    CHECK(pose.pitch == static_cast<float>(in.pitch));

    // ...and nothing else in the input can move them.
    for (double speed : {0.0, 0.05, 0.2, 1.0}) {
        for (int hurt : {0, 1, 10}) {
            for (int fuse : {0, 5, 30}) {
                for (bool baby : {false, true}) {
                    MobPoseInput other = in;
                    other.speed = speed;
                    other.hurt_cooldown = hurt;
                    other.fuse = fuse;
                    other.baby = baby;
                    const MobPose p = mob_pose(other, 0.4f);
                    CHECK(p.yaw == static_cast<float>(in.yaw));
                    CHECK(p.pitch == static_cast<float>(in.pitch));
                }
            }
        }
    }
}

TEST_CASE("T-B1 pose: the head keeps the simulation's pitch, the limbs stride") {
    MobPoseInput in;
    in.yaw = 0.0;
    in.pitch = 0.5; // looking down (the sign view_dir() uses)
    in.speed = 0.16;
    in.on_ground = true;

    const MobPose quarter = mob_pose(in, 1.5707963f); // sin = +1
    const MobPose three_quarter = mob_pose(in, 3.1415927f + 1.5707963f);

    // A positive pitch looks down, and a positive rotation about the local +X
    // lifts the head's -Z front, so the head angle is the negated pitch.
    CHECK(quarter.swing_x[MobJoint::kMobJointHead] == doctest::Approx(-0.5f));

    // Legs alternate: at the top of the swing one is forward and the other back.
    CHECK(quarter.swing_x[MobJoint::kMobJointLegLeft] > 0.0f);
    CHECK(quarter.swing_x[MobJoint::kMobJointLegRight] < 0.0f);
    CHECK(three_quarter.swing_x[MobJoint::kMobJointLegLeft] < 0.0f);
    CHECK(three_quarter.swing_x[MobJoint::kMobJointLegRight] > 0.0f);
    CHECK(quarter.swing_x[MobJoint::kMobJointLegLeft] ==
          doctest::Approx(-quarter.swing_x[MobJoint::kMobJointLegRight]));

    // One full stride travels through zero: legs are together twice per cycle.
    const MobPose zero = mob_pose(in, 0.0f);
    CHECK(zero.swing_x[MobJoint::kMobJointLegLeft] == doctest::Approx(0.0f));

    SUBCASE("swing grows with the simulated speed and then clamps") {
        MobPoseInput slow = in;
        slow.speed = 0.04;
        MobPoseInput fast = in;
        fast.speed = 0.16;
        MobPoseInput faster = in;
        faster.speed = 2.0;
        const float a_slow = mob_pose(slow, 1.5707963f).swing_x[MobJoint::kMobJointLegLeft];
        const float a_fast = mob_pose(fast, 1.5707963f).swing_x[MobJoint::kMobJointLegLeft];
        const float a_faster = mob_pose(faster, 1.5707963f).swing_x[MobJoint::kMobJointLegLeft];
        CHECK(a_slow > 0.0f);
        CHECK(a_slow < a_fast);
        CHECK(a_faster == doctest::Approx(a_fast)); // clamped at kMobFullStrideSpeed
    }

    SUBCASE("standing still and airborne are both a rest pose") {
        MobPoseInput still = in;
        still.speed = 0.0;
        const MobPose rest = mob_pose(still, 1.5707963f);
        CHECK(rest.body_offset.y == 0.0f);
        CHECK(std::abs(rest.swing_x[MobJoint::kMobJointLegLeft]) <
              std::abs(mob_pose(in, 1.5707963f).swing_x[MobJoint::kMobJointLegLeft]));

        MobPoseInput air = in;
        air.on_ground = false;
        CHECK(mob_pose(air, 1.5707963f).body_offset.y == 0.0f);
    }
}

TEST_CASE("T-B1 pose: hurt and fuse change presentation only") {
    MobPoseInput in;
    in.speed = 0.1;
    in.on_ground = true;

    SUBCASE("no hurt, no fuse: a neutral tint and scale") {
        const MobPose pose = mob_pose(in, 0.5f);
        CHECK(pose.tint == glm::vec4(1.0f));
        CHECK(pose.scale == doctest::Approx(1.0f));
    }

    SUBCASE("hurt_cooldown drives the flash and nothing else") {
        in.hurt_cooldown = 6;
        const MobPose hurt = mob_pose(in, 0.5f);
        CHECK(hurt.tint.r == doctest::Approx(1.0f));
        CHECK(hurt.tint.g < 1.0f);
        CHECK(hurt.scale == doctest::Approx(1.0f));
        in.hurt_cooldown = 0;
        CHECK(mob_pose(in, 0.5f).tint == glm::vec4(1.0f));
    }

    SUBCASE("a burning fuse pulses the scale") {
        in.fuse = 30;
        const MobPose burning = mob_pose(in, 0.5f);
        CHECK(burning.scale != doctest::Approx(1.0f));
        CHECK(burning.tint.b < 1.0f);
        in.fuse = 0;
        CHECK(mob_pose(in, 0.5f).scale == doctest::Approx(1.0f));
    }

    SUBCASE("baby is a render-side scale of a simulation flag") {
        in.baby = true;
        CHECK(mob_pose(in, 0.5f).scale == doctest::Approx(opencraft::client::kMobBabyScale));
    }
}

TEST_CASE("T-B1 pose: the walk clock advances with distance, not with time") {
    MobAnimClock clock;

    SUBCASE("speed advances the phase, distance-proportionally") {
        const float first = clock.advance(7, 0.1, 0.1f);
        const float second = clock.advance(7, 0.1, 0.1f);
        CHECK(first > 0.0f);
        CHECK(second == doctest::Approx(2.0f * first)); // same speed, same step
    }

    SUBCASE("a stopped mob holds its phase instead of walking on the spot") {
        const float moving = clock.advance(7, 0.2, 0.1f);
        CHECK(clock.advance(7, 0.0, 0.1f) == doctest::Approx(moving));
        CHECK(clock.advance(7, 0.0, 10.0f) == doctest::Approx(moving));
    }

    SUBCASE("phase stays inside one turn") {
        float phase = 0.0f;
        for (int i = 0; i < 200; ++i) {
            phase = clock.advance(7, 0.5, 0.05f);
            CHECK(phase >= 0.0f);
            CHECK(phase < 6.2831853f);
        }
    }

    SUBCASE("instances that stop being drawn are forgotten") {
        clock.advance(7, 0.3, 0.1f);
        clock.end_frame();
        clock.advance(9, 0.3, 0.1f); // 7 is not drawn this frame
        clock.end_frame();
        // A fresh entity reusing slot semantics starts from rest rather than
        // inheriting the previous owner's stride.
        CHECK(clock.advance(7, 0.0, 0.1f) == doctest::Approx(0.0f));
    }
}
