// T009 persistence tests: region file roundtrip + corruption detection,
// level file roundtrip, WorldSave orchestration, and the zstd compression
// ratio of a typical generated chunk (vs the T003 8253-byte reference).
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <zstd.h>

#include "opencraft/core/byte_buffer.hpp"
#include "opencraft/storage/level_file.hpp"
#include "opencraft/storage/region_file.hpp"
#include "opencraft/storage/world_save.hpp"
#include "opencraft/voxel/chunk.hpp"
#include "opencraft/worldgen/terrain_generator.hpp"

namespace fs = std::filesystem;
using opencraft::core::ByteBuffer;
using opencraft::storage::LevelData;
using opencraft::storage::RegionFile;
using opencraft::storage::WorldSave;
using opencraft::voxel::Chunk;

namespace {

std::vector<std::uint8_t> make_payload(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> data(size);
    for (std::size_t i = 0; i < size; ++i) {
        data[i] = static_cast<std::uint8_t>(seed + i * 7);
    }
    return data;
}

fs::path temp_dir(const std::string &name) {
    const fs::path dir = fs::temp_directory_path() / ("opencraft_storage_test_" + name);
    fs::remove_all(dir);
    return dir;
}

} // namespace

TEST_CASE("region file roundtrip: multiple chunks survive save and reopen") {
    const fs::path dir = temp_dir("roundtrip");
    RegionFile region;
    const std::vector<std::uint8_t> a = make_payload(1000, 1);
    const std::vector<std::uint8_t> b = make_payload(9000, 2);  // > 1 sector
    const std::vector<std::uint8_t> c = make_payload(20000, 3); // > 5 sectors
    region.write_chunk(0, 0, a.data(), a.size(), 100);
    region.write_chunk(31, 31, b.data(), b.size(), 200);
    region.write_chunk(5, 17, c.data(), c.size(), 300);
    region.save(dir / "region" / "r.0.0.ocr");

    const RegionFile reopened = RegionFile::open(dir / "region" / "r.0.0.ocr");
    CHECK(reopened.chunk_exists(0, 0));
    CHECK(reopened.chunk_exists(31, 31));
    CHECK(reopened.chunk_exists(5, 17));
    CHECK_FALSE(reopened.chunk_exists(1, 1));
    CHECK(reopened.stored_chunk_count() == 3);
    CHECK(reopened.read_chunk(0, 0) == a);
    CHECK(reopened.read_chunk(31, 31) == b);
    CHECK(reopened.read_chunk(5, 17) == c);
}

TEST_CASE("region file rewrite of the same chunk replaces the value") {
    const fs::path dir = temp_dir("rewrite");
    RegionFile region;
    const std::vector<std::uint8_t> v1 = make_payload(500, 10);
    const std::vector<std::uint8_t> v2 = make_payload(9000, 20);
    region.write_chunk(3, 4, v1.data(), v1.size(), 1);
    region.write_chunk(3, 4, v2.data(), v2.size(), 2);
    region.save(dir / "r.0.0.ocr");

    const RegionFile reopened = RegionFile::open(dir / "r.0.0.ocr");
    CHECK(reopened.stored_chunk_count() == 1);
    CHECK(reopened.read_chunk(3, 4) == v2);
    CHECK(reopened.chunk_exists(3, 4));
}

TEST_CASE("region file cross-sector growth keeps earlier chunks readable") {
    const fs::path dir = temp_dir("growth");
    RegionFile region;
    const std::vector<std::uint8_t> small = make_payload(100, 30);
    region.write_chunk(0, 0, small.data(), small.size(), 1);
    region.save(dir / "r.0.0.ocr");

    // A later chunk grows the file across many sectors.
    RegionFile region2 = RegionFile::open(dir / "r.0.0.ocr");
    const std::vector<std::uint8_t> huge = make_payload(30000, 40);
    region2.write_chunk(15, 15, huge.data(), huge.size(), 2);
    region2.save(dir / "r.0.0.ocr");

    const RegionFile reopened = RegionFile::open(dir / "r.0.0.ocr");
    CHECK(reopened.read_chunk(0, 0) == small);
    CHECK(reopened.read_chunk(15, 15) == huge);
    CHECK(reopened.stored_chunk_count() == 2);
}

TEST_CASE("region file corruption is detected and refused") {
    const fs::path dir = temp_dir("corrupt");
    RegionFile region;
    const std::vector<std::uint8_t> a = make_payload(8000, 50);
    region.write_chunk(0, 0, a.data(), a.size(), 1);
    const fs::path path = dir / "r.0.0.ocr";
    region.save(path);

    std::vector<std::uint8_t> image(fs::file_size(path));
    {
        std::ifstream in(path, std::ios::binary);
        in.read(reinterpret_cast<char *>(image.data()), static_cast<std::streamsize>(image.size()));
    }

    auto rewrite = [&](const std::vector<std::uint8_t> &bytes) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    };

    SUBCASE("truncated file") {
        rewrite(std::vector<std::uint8_t>(image.begin(), image.begin() + 4096));
        CHECK_THROWS_AS(RegionFile::open(path), std::runtime_error);
    }
    SUBCASE("bad magic") {
        std::vector<std::uint8_t> copy = image;
        copy[0] ^= 0xFF;
        rewrite(copy);
        CHECK_THROWS_AS(RegionFile::open(path), std::runtime_error);
    }
    SUBCASE("bad format version") {
        std::vector<std::uint8_t> copy = image;
        copy[4] = 0x7F; // version field; also breaks the header CRC
        rewrite(copy);
        CHECK_THROWS_AS(RegionFile::open(path), std::runtime_error);
    }
    SUBCASE("header checksum mismatch") {
        std::vector<std::uint8_t> copy = image;
        copy[6000] ^= 0x01; // inside the timestamp table
        rewrite(copy);
        CHECK_THROWS_AS(RegionFile::open(path), std::runtime_error);
    }
    SUBCASE("block payload corruption") {
        std::vector<std::uint8_t> copy = image;
        copy[8192 + 20 + 100] ^= 0x01; // first block, inside compressed payload
        rewrite(copy);
        // open() validates every block CRC eagerly, so the file is refused
        // at load time, not lazily at first read.
        CHECK_THROWS_AS(RegionFile::open(path), std::runtime_error);
    }
}

TEST_CASE("level file roundtrip preserves every field") {
    const fs::path dir = temp_dir("level");
    LevelData level;
    level.seed = 0x4F50454E43524146ULL;
    level.tick_count = 123456;
    level.has_player = true;
    level.spawn_x = 8.0;
    level.spawn_y = 64.0;
    level.spawn_z = 8.0;
    level.player_x = 10.5;
    level.player_y = 70.25;
    level.player_z = -3.5;
    level.player_vx = 0.1;
    level.player_vy = -0.2;
    level.player_vz = 0.3;
    level.yaw = 1.25;
    level.pitch = -0.5;
    level.health = 13.5;
    level.fall_peak_y = 80.0;
    level.fall_distance = 9.75;
    level.pose = 1;
    level.on_ground = false;
    level.selected_block = 7;

    const fs::path path = dir / "world" / "level.ocd";
    opencraft::storage::write_level(path, level);
    const LevelData loaded = opencraft::storage::read_level(path);

    CHECK(loaded.seed == level.seed);
    CHECK(loaded.tick_count == level.tick_count);
    CHECK(loaded.has_player == level.has_player);
    CHECK(loaded.spawn_x == level.spawn_x);
    CHECK(loaded.spawn_y == level.spawn_y);
    CHECK(loaded.spawn_z == level.spawn_z);
    CHECK(loaded.player_x == level.player_x);
    CHECK(loaded.player_y == level.player_y);
    CHECK(loaded.player_z == level.player_z);
    CHECK(loaded.player_vx == level.player_vx);
    CHECK(loaded.player_vy == level.player_vy);
    CHECK(loaded.player_vz == level.player_vz);
    CHECK(loaded.yaw == level.yaw);
    CHECK(loaded.pitch == level.pitch);
    CHECK(loaded.health == level.health);
    CHECK(loaded.fall_peak_y == level.fall_peak_y);
    CHECK(loaded.fall_distance == level.fall_distance);
    CHECK(loaded.pose == level.pose);
    CHECK(loaded.on_ground == level.on_ground);
    CHECK(loaded.selected_block == level.selected_block);
}

TEST_CASE("level file corruption is detected and refused") {
    const fs::path dir = temp_dir("level_corrupt");
    const fs::path path = dir / "level.ocd";
    opencraft::storage::write_level(path, LevelData{});

    SUBCASE("bad magic") {
        std::vector<std::uint8_t> image(fs::file_size(path));
        {
            std::ifstream in(path, std::ios::binary);
            in.read(reinterpret_cast<char *>(image.data()), static_cast<std::streamsize>(image.size()));
        }
        image[0] ^= 0xFF;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
    }
    SUBCASE("truncated") {
        std::vector<std::uint8_t> image(fs::file_size(path));
        {
            std::ifstream in(path, std::ios::binary);
            in.read(reinterpret_cast<char *>(image.data()), static_cast<std::streamsize>(image.size()));
        }
        image.resize(image.size() - 20); // cuts into the payload and CRC area
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
    }
    SUBCASE("payload tampered") {
        std::vector<std::uint8_t> image(fs::file_size(path));
        {
            std::ifstream in(path, std::ios::binary);
            in.read(reinterpret_cast<char *>(image.data()), static_cast<std::streamsize>(image.size()));
        }
        image[16 + 2] ^= 0x01; // inside the payload (seed low byte area)
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
    }
    CHECK_THROWS_AS(opencraft::storage::read_level(path), std::runtime_error);
}

TEST_CASE("WorldSave stores and reloads chunk payloads through region files") {
    const fs::path root = temp_dir("worldsave");
    {
        WorldSave save(root, "world");
        const std::vector<std::uint8_t> a = make_payload(5000, 60);
        const std::vector<std::uint8_t> b = make_payload(15000, 70);
        save.store_chunk_sync(0, 0, a.data(), a.size());
        save.store_chunk_sync(-1, -3, b.data(), b.size());
        save.mark_dirty(2, 2); // never stored: flush must not lose the marker silently
        CHECK(save.pending_dirty_count() == 1);
        CHECK(save.take_dirty().size() == 1);
        CHECK(save.pending_dirty_count() == 0);
    }
    WorldSave reopened(root, "world");
    const std::optional<std::vector<std::uint8_t>> a = reopened.load_chunk(0, 0);
    const std::optional<std::vector<std::uint8_t>> b = reopened.load_chunk(-1, -3);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == make_payload(5000, 60));
    CHECK(*b == make_payload(15000, 70));
    CHECK_FALSE(reopened.load_chunk(9, 9).has_value());
    CHECK(reopened.cached_region_count() >= 2);
}

TEST_CASE("WorldSave async writes complete on flush") {
    const fs::path root = temp_dir("worldsave_async");
    WorldSave save(root, "world");
    for (int i = 0; i < 8; ++i) {
        const std::vector<std::uint8_t> payload = make_payload(2000 + static_cast<std::size_t>(i) * 1000, 80);
        save.store_chunk_async(i, -i, payload);
    }
    save.flush();
    for (int i = 0; i < 8; ++i) {
        const std::optional<std::vector<std::uint8_t>> loaded = save.load_chunk(i, -i);
        REQUIRE(loaded.has_value());
        CHECK(*loaded == make_payload(2000 + static_cast<std::size_t>(i) * 1000, 80));
    }
}

TEST_CASE("WorldSave autosave cadence fires once per window") {
    const fs::path root = temp_dir("worldsave_cadence");
    WorldSave save(root, "world");
    int fires = 0;
    for (int i = 0; i < static_cast<int>(WorldSave::kAutosaveTicks) * 2; ++i) {
        if (save.maybe_autosave_tick()) {
            ++fires;
        }
    }
    CHECK(fires == 2);
}

TEST_CASE("WorldSave quarantines a corrupt region file and keeps saving") {
    const fs::path root = temp_dir("worldsave_corrupt");
    const fs::path region_path = root / "world" / "region" / "r.0.0.ocr";
    {
        WorldSave save(root, "world");
        const std::vector<std::uint8_t> payload = make_payload(3000, 90);
        save.store_chunk_sync(0, 0, payload.data(), payload.size());
    }
    // Corrupt the stored file beyond repair.
    std::vector<std::uint8_t> image(fs::file_size(region_path));
    {
        std::ifstream in(region_path, std::ios::binary);
        in.read(reinterpret_cast<char *>(image.data()), static_cast<std::streamsize>(image.size()));
    }
    image[8192 + 30] ^= 0xFF;
    {
        std::ofstream out(region_path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
    }
    WorldSave save(root, "world");
    // The corrupt file is refused: its chunks count as absent (regenerate).
    CHECK(save.load_chunk(0, 0) == std::nullopt);
    // The bad file is quarantined as .ocr.bad and a fresh region takes over;
    // new writes land and survive a reload.
    const std::vector<std::uint8_t> payload2 = make_payload(3100, 91);
    save.store_chunk_sync(0, 0, payload2.data(), payload2.size());
    save.flush();
    CHECK(fs::exists(root / "world" / "region" / "r.0.0.ocr.bad"));
    WorldSave reloaded(root, "world");
    const std::optional<std::vector<std::uint8_t>> chunk = reloaded.load_chunk(0, 0);
    REQUIRE(chunk.has_value());
    CHECK(*chunk == payload2);
}

TEST_CASE("zstd compression ratio of a typical generated chunk") {
    opencraft::voxel::BlockRegistry registry = opencraft::voxel::BlockRegistry::create_default();
    opencraft::worldgen::TerrainGenerator generator(0x4F50454E43524146ULL, registry);
    Chunk chunk;
    generator.generate_chunk(0, 0, chunk);

    ByteBuffer raw;
    chunk.serialize(raw);
    const std::size_t raw_size = raw.size();

    std::vector<std::uint8_t> compressed(ZSTD_compressBound(raw_size));
    const std::size_t compressed_size = ZSTD_compress(compressed.data(), compressed.size(), raw.data(), raw_size, 3);
    REQUIRE(!ZSTD_isError(compressed_size));

    const double ratio = static_cast<double>(compressed_size) / static_cast<double>(raw_size);
    MESSAGE("typical chunk raw=" << raw_size << " bytes (T003 reference: 8253), zstd(level 3)=" << compressed_size
                                 << " bytes, ratio=" << ratio);
    CHECK(compressed_size < raw_size);
    CHECK(ratio < 0.5); // palette-packed terrain should compress well past 2x
}
