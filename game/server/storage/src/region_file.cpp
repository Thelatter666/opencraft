#include "opencraft/storage/region_file.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include <zstd.h>

namespace opencraft::storage {

namespace {

// Table-driven CRC-32 (zlib polynomial 0xEDB88320). Local to the storage lib:
// the only checksum user for now; a shared core utility can host it later.
std::uint32_t crc32(const std::uint8_t *data, std::size_t size) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void put_u16(std::uint8_t *out, std::uint16_t v) {
    out[0] = static_cast<std::uint8_t>(v);
    out[1] = static_cast<std::uint8_t>(v >> 8);
}

void put_u32(std::uint8_t *out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
    }
}

std::uint16_t get_u16(const std::uint8_t *in) {
    return static_cast<std::uint16_t>(in[0] | (static_cast<std::uint16_t>(in[1]) << 8));
}

std::uint32_t get_u32(const std::uint8_t *in) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(in[i]) << (8 * i);
    }
    return v;
}

std::vector<std::uint8_t> zstd_compress(const std::uint8_t *data, std::size_t size) {
    const std::size_t bound = ZSTD_compressBound(size);
    std::vector<std::uint8_t> out(bound);
    const std::size_t written = ZSTD_compress(out.data(), bound, data, size, 3);
    if (ZSTD_isError(written) != 0) {
        throw std::runtime_error(std::string("zstd compress failed: ") + ZSTD_getErrorName(written));
    }
    out.resize(written);
    return out;
}

std::vector<std::uint8_t> zstd_decompress(const std::uint8_t *data, std::size_t size,
                                          std::size_t uncompressed_size) {
    std::vector<std::uint8_t> out(uncompressed_size);
    const std::size_t written = ZSTD_decompress(out.data(), uncompressed_size, data, size);
    if (ZSTD_isError(written) != 0) {
        throw std::runtime_error(std::string("zstd decompress failed: ") + ZSTD_getErrorName(written));
    }
    if (written != uncompressed_size) {
        throw std::runtime_error("zstd decompress size mismatch: header says " +
                                 std::to_string(uncompressed_size) + ", got " + std::to_string(written));
    }
    return out;
}

[[noreturn]] void corrupt(const std::string &what) {
    throw std::runtime_error("corrupt region file: " + what);
}

void fsync_file(std::FILE *file) {
    if (::fflush(file) != 0) {
        throw std::runtime_error("fflush failed");
    }
    if (::fsync(::fileno(file)) != 0) {
        throw std::runtime_error("fsync failed");
    }
}

} // namespace

std::string RegionFile::file_name(int rx, int rz) {
    return "r." + std::to_string(rx) + "." + std::to_string(rz) + ".ocr";
}

int RegionFile::index_of(int local_x, int local_z) {
    if (local_x < 0 || local_x >= kSide || local_z < 0 || local_z >= kSide) {
        return -1;
    }
    return local_z * kSide + local_x;
}

std::size_t RegionFile::stored_chunk_count() const {
    std::size_t count = 0;
    for (const Block &b : blocks_) {
        if (!b.raw.empty()) {
            ++count;
        }
    }
    return count;
}

RegionFile RegionFile::open(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("region file not readable: " + path.string());
    }
    in.seekg(0, std::ios::end);
    const std::streamoff file_size = in.tellg();
    if (file_size < static_cast<std::streamoff>(kHeaderSize)) {
        corrupt("file is " + std::to_string(file_size) + " bytes, smaller than the 8 KiB header");
    }
    std::vector<std::uint8_t> header(static_cast<std::size_t>(kHeaderSize));
    in.seekg(0);
    in.read(reinterpret_cast<char *>(header.data()), static_cast<std::streamsize>(header.size()));
    if (!in) {
        corrupt("short read on header");
    }

    if (get_u32(header.data()) != kMagic) {
        corrupt("bad magic");
    }
    if (get_u16(header.data() + 4) != kFormatVersion) {
        corrupt("unsupported format version " + std::to_string(get_u16(header.data() + 4)));
    }
    std::uint8_t crc_scratch[4] = {0, 0, 0, 0};
    std::memcpy(crc_scratch, header.data() + 12, 4);
    std::memset(header.data() + 12, 0, 4);
    const std::uint32_t expected_crc = get_u32(crc_scratch);
    if (crc32(header.data(), header.size()) != expected_crc) {
        corrupt("header checksum mismatch");
    }

    RegionFile region;
    for (int i = 0; i < kChunkCount; ++i) {
        const std::uint8_t *entry = header.data() + 16 + static_cast<std::size_t>(i) * 4;
        const std::uint32_t sector = get_u32(entry) >> 8; // u24
        const std::uint32_t sector_count = entry[3];
        if (sector == 0 || sector_count == 0) {
            continue;
        }
        const std::size_t offset = static_cast<std::size_t>(sector) * kSectorSize;
        const std::size_t span = static_cast<std::size_t>(sector_count) * kSectorSize;
        if (offset < kHeaderSize || offset + span > static_cast<std::size_t>(file_size)) {
            corrupt("location entry " + std::to_string(i) + " points outside the file");
        }

        std::vector<std::uint8_t> block_bytes(span);
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(reinterpret_cast<char *>(block_bytes.data()), static_cast<std::streamsize>(span));
        if (!in) {
            corrupt("short read on block " + std::to_string(i));
        }
        if (get_u32(block_bytes.data()) != kBlockMagic) {
            corrupt("block " + std::to_string(i) + " bad block magic");
        }
        const std::uint32_t raw_len = get_u32(block_bytes.data() + 4);
        if (raw_len == 0 || raw_len > 16u * 1024u * 1024u) {
            corrupt("block " + std::to_string(i) + " implausible uncompressed length " + std::to_string(raw_len));
        }
        if (block_bytes[8] != kCompressionZstd) {
            corrupt("block " + std::to_string(i) + " unknown compression type " + std::to_string(block_bytes[8]));
        }
        // Block header is 20 bytes: magic(4) uncompressed_len(4) comp_type(1)
        // reserved(3) compressed_len(4) payload_crc(4); payload follows at 20.
        const std::uint32_t compressed_len = get_u32(block_bytes.data() + 12);
        if (compressed_len == 0 || 20u + compressed_len > span) {
            corrupt("block " + std::to_string(i) + " compressed length overruns its sectors");
        }
        if (crc32(block_bytes.data() + 20, compressed_len) != get_u32(block_bytes.data() + 16)) {
            corrupt("block " + std::to_string(i) + " payload checksum mismatch");
        }
        // Full decode validates the zstd frame itself and restores the
        // uncompressed payload (blocks_ always holds uncompressed bytes).
        Block &slot = region.blocks_[i];
        slot.raw = zstd_decompress(block_bytes.data() + 20, compressed_len, raw_len);
        slot.timestamp = get_u32(header.data() + 4096 + static_cast<std::size_t>(i) * 4);
    }
    return region;
}

bool RegionFile::chunk_exists(int local_x, int local_z) const {
    const int index = index_of(local_x, local_z);
    return index >= 0 && !blocks_[static_cast<std::size_t>(index)].raw.empty();
}

std::vector<std::uint8_t> RegionFile::read_chunk(int local_x, int local_z) const {
    const int index = index_of(local_x, local_z);
    if (index < 0) {
        corrupt("local chunk (" + std::to_string(local_x) + ", " + std::to_string(local_z) + ") out of range");
    }
    const Block &block = blocks_[static_cast<std::size_t>(index)];
    if (block.raw.empty()) {
        corrupt("chunk (" + std::to_string(local_x) + ", " + std::to_string(local_z) + ") not present");
    }
    return block.raw;
}

void RegionFile::write_chunk(int local_x, int local_z, const std::uint8_t *data, std::size_t size,
                             std::uint32_t timestamp) {
    const int index = index_of(local_x, local_z);
    if (index < 0) {
        return; // out of the 32x32 window: caller must route to another file
    }
    Block &block = blocks_[static_cast<std::size_t>(index)];
    block.raw.assign(data, data + size);
    block.timestamp = timestamp;
}

std::vector<std::uint8_t> RegionFile::serialize_image() const {
    // Header.
    std::vector<std::uint8_t> image(kHeaderSize, 0);
    put_u32(image.data(), kMagic);
    put_u16(image.data() + 4, kFormatVersion);
    put_u16(image.data() + 6, static_cast<std::uint16_t>(kHeaderSize));
    put_u32(image.data() + 8, 0); // flags

    // Blocks: append at the next free sector, remember location entries.
    std::vector<std::uint32_t> location(kChunkCount, 0);
    std::uint32_t next_sector = kHeaderSize / kSectorSize; // 2
    for (int i = 0; i < kChunkCount; ++i) {
        const Block &block = blocks_[static_cast<std::size_t>(i)];
        if (block.raw.empty()) {
            continue;
        }
        const std::vector<std::uint8_t> compressed = zstd_compress(block.raw.data(), block.raw.size());
        const std::uint32_t compressed_len = static_cast<std::uint32_t>(compressed.size());
        const std::size_t span_bytes = 16 + compressed_len;
        const std::uint32_t sector_count =
            static_cast<std::uint32_t>((span_bytes + kSectorSize - 1) / kSectorSize);

        const std::size_t offset = static_cast<std::size_t>(next_sector) * kSectorSize;
        if (image.size() < offset + static_cast<std::size_t>(sector_count) * kSectorSize) {
            image.resize(offset + static_cast<std::size_t>(sector_count) * kSectorSize, 0);
        }
        std::uint8_t *block = image.data() + offset;
        put_u32(block, kBlockMagic);
        put_u32(block + 4, static_cast<std::uint32_t>(block.raw.size())); // uncompressed
        block[8] = kCompressionZstd;
        block[9] = 0;
        block[10] = 0;
        block[11] = 0;
        put_u32(block + 12, compressed_len);
        put_u32(block + 16, crc32(compressed.data(), compressed.size()));
        std::memcpy(block + 20, compressed.data(), compressed.size());

        location[static_cast<std::size_t>(i)] =
            (next_sector << 8) | (sector_count & 0xFF);
        put_u32(image.data() + 4096 + static_cast<std::size_t>(i) * 4, block.timestamp);
        next_sector += sector_count;
    }
    for (int i = 0; i < kChunkCount; ++i) {
        put_u32(image.data() + 16 + static_cast<std::size_t>(i) * 4, location[static_cast<std::size_t>(i)]);
    }

    // Header CRC over all 8 KiB with the CRC field itself zeroed.
    put_u32(image.data() + 12, 0);
    put_u32(image.data() + 12, crc32(image.data(), kHeaderSize));
    return image;
}

void RegionFile::save(const std::filesystem::path &target) const {
    const std::vector<std::uint8_t> image = serialize_image();
    std::filesystem::path parent = target.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    std::filesystem::create_directories(parent);
    const std::filesystem::path temp = target.string() + ".tmp";
    std::FILE *file = std::fopen(temp.c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open temp file " + temp.string());
    }
    try {
        if (std::fwrite(image.data(), 1, image.size(), file) != image.size()) {
            throw std::runtime_error("short write on " + temp.string());
        }
        fsync_file(file);
        if (std::fclose(file) != 0) {
            file = nullptr;
            throw std::runtime_error("fclose failed on " + temp.string());
        }
        file = nullptr;
    } catch (...) {
        if (file != nullptr) {
            std::fclose(file);
        }
        std::filesystem::remove(temp);
        throw;
    }
    std::filesystem::rename(temp, target);
    // Persist the rename itself.
    std::FILE *dir = std::fopen(parent.c_str(), "rb");
    if (dir != nullptr) {
        (void)::fsync(::fileno(dir));
        std::fclose(dir);
    }
}

} // namespace opencraft::storage
