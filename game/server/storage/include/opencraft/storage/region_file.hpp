#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace opencraft::storage {

// Region file for the OpenCraft save format (T009; docs/02 §5, docs/03 §7,
// design motivation research/02 §6.1 + research/03 §5.1). ONE file aggregates
// a 32x32 group of chunks. Custom format, deliberately NOT Anvil-compatible;
// adopted design ideas are the aggregation, 4 KiB sectors and a per-block
// uncompressed-length header.
//
// On-disk layout (little-endian):
//   offset 0    : magic "OCRF" (u32)
//   offset 4    : u16 format version (= 1)
//   offset 6    : u16 header size in bytes (= 8192, future-proof)
//   offset 8    : u32 flags (= 0)
//   offset 12   : u32 header CRC-32 over all 8192 header bytes with this
//                 field zeroed - the header is self-verifying as the card
//                 requires (magic + version + checksum).
//   offset 16   : location table, 1024 entries x 4 bytes:
//                 u24 sector index of the chunk block (0 = absent),
//                 u8 sector count (0 = absent).
//   offset 4104 : timestamp table, 1024 x u32 (last-write unix time, purely
//                 informational).
//   offset 8192 : sector 0; each sector is 4096 bytes.
//
// A stored chunk ("block") starts on a sector boundary:
//   u32 block magic "OCCB", u32 uncompressed length, u8 compression type
//   (1 = zstd), 3 reserved bytes, u32 compressed length, u32 CRC-32 of the
//   COMPRESSED payload, then the compressed bytes, padded to the next sector
//   boundary (block header = 20 bytes).
//
// Error model: every parse problem (bad magic/version/header CRC, truncated
// file, bad block magic/length/CRC) throws std::runtime_error - callers are
// expected to catch, log and refuse the file, never crash.
//
// Memory model: the whole region is held in RAM (header tables + stored chunk
// payloads) and save() rewrites the whole file through a temp file + fsync +
// atomic rename, which makes every snapshot crash-atomic. Rewriting a chunk
// appends a fresh block; superseded sectors become holes (compaction is a
// later task, research/03 §5.3). Milestone-scale worlds fit comfortably.
class RegionFile {
public:
    static constexpr int kSide = 32;                    // chunks per file edge
    static constexpr int kChunkCount = kSide * kSide;   // 1024
    static constexpr std::uint32_t kSectorSize = 4096;
    static constexpr std::uint32_t kHeaderSize = 8 * kSectorSize; // 8 KiB
    static constexpr std::uint32_t kMagic = 0x4F435246;           // "OCRF"
    static constexpr std::uint32_t kBlockMagic = 0x4F434342;      // "OCCB"
    static constexpr std::uint16_t kFormatVersion = 1;
    static constexpr std::uint8_t kCompressionZstd = 1;

    // "r.<rx>.<rz>.ocr", rx/rz may be negative (e.g. "r.-1.0.ocr").
    [[nodiscard]] static std::string file_name(int rx, int rz);

    RegionFile() = default;

    // Parses an existing file; throws std::runtime_error on any corruption.
    // (No [[nodiscard]]: calling it just to validate a file is legitimate.)
    static RegionFile open(const std::filesystem::path &path);

    [[nodiscard]] bool chunk_exists(int local_x, int local_z) const;

    // Returns the UNCOMPRESSED chunk payload. Throws std::runtime_error when
    // absent or corrupt.
    [[nodiscard]] std::vector<std::uint8_t> read_chunk(int local_x, int local_z) const;

    // Compresses with zstd and stores (appends). No effect for out-of-range
    // local coordinates (the 1024-slot table cannot address them anyway).
    void write_chunk(int local_x, int local_z, const std::uint8_t *data, std::size_t size,
                     std::uint32_t timestamp);

    // Writes the full on-disk image atomically: target.tmp -> fsync -> rename
    // -> fsync parent directory. Creates parent directories as needed.
    void save(const std::filesystem::path &target) const;

    // Testing seam: the exact bytes save() would write.
    [[nodiscard]] std::vector<std::uint8_t> serialize_image() const;

    [[nodiscard]] std::size_t stored_chunk_count() const;

private:
    struct Block {
        std::vector<std::uint8_t> raw; // compressed payload, no header
        std::uint32_t timestamp = 0;
    };

    static int index_of(int local_x, int local_z);

    std::vector<Block> blocks_ = std::vector<Block>(kChunkCount);
};

} // namespace opencraft::storage
