#include "opencraft/storage/level_file.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

namespace opencraft::storage {

namespace {

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

double get_f64(core::ByteBuffer &buffer) {
    const std::uint64_t bits = buffer.read_u64();
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void put_f64(core::ByteBuffer &buffer, double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    buffer.write_u64(bits);
}

[[noreturn]] void corrupt(const std::string &what) {
    throw std::runtime_error("corrupt level file: " + what);
}

} // namespace

core::ByteBuffer serialize_level(const LevelData &level) {
    core::ByteBuffer payload;
    payload.write_u64(level.seed);
    payload.write_u64(level.tick_count);
    payload.write_u8(level.has_player ? 1 : 0);
    put_f64(payload, level.spawn_x);
    put_f64(payload, level.spawn_y);
    put_f64(payload, level.spawn_z);
    put_f64(payload, level.player_x);
    put_f64(payload, level.player_y);
    put_f64(payload, level.player_z);
    put_f64(payload, level.player_vx);
    put_f64(payload, level.player_vy);
    put_f64(payload, level.player_vz);
    put_f64(payload, level.yaw);
    put_f64(payload, level.pitch);
    put_f64(payload, level.health);
    put_f64(payload, level.fall_peak_y);
    put_f64(payload, level.fall_distance);
    payload.write_u8(level.pose);
    payload.write_u8(level.on_ground ? 1 : 0);
    payload.write_u16(level.selected_block);
    return payload;
}

LevelData deserialize_level(core::ByteBuffer &payload) {
    LevelData level;
    level.seed = payload.read_u64();
    level.tick_count = payload.read_u64();
    level.has_player = payload.read_u8() != 0;
    level.spawn_x = get_f64(payload);
    level.spawn_y = get_f64(payload);
    level.spawn_z = get_f64(payload);
    level.player_x = get_f64(payload);
    level.player_y = get_f64(payload);
    level.player_z = get_f64(payload);
    level.player_vx = get_f64(payload);
    level.player_vy = get_f64(payload);
    level.player_vz = get_f64(payload);
    level.yaw = get_f64(payload);
    level.pitch = get_f64(payload);
    level.health = get_f64(payload);
    level.fall_peak_y = get_f64(payload);
    level.fall_distance = get_f64(payload);
    level.pose = payload.read_u8();
    level.on_ground = payload.read_u8() != 0;
    level.selected_block = payload.read_u16();
    if (payload.remaining() != 0) {
        corrupt(std::to_string(payload.remaining()) + " trailing bytes in payload");
    }
    return level;
}

LevelData read_level(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("level file not readable: " + path.string());
    }
    in.seekg(0, std::ios::end);
    const std::streamoff file_size = in.tellg();
    if (file_size < 12) {
        corrupt("file is " + std::to_string(file_size) + " bytes, smaller than the 12-byte header");
    }
    in.seekg(0);
    std::uint8_t header[12] = {};
    in.read(reinterpret_cast<char *>(header), sizeof(header));
    if (!in) {
        corrupt("short read on header");
    }
    auto get_u32 = [](const std::uint8_t *p) {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
        }
        return v;
    };
    auto get_u16 = [](const std::uint8_t *p) {
        return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
    };
    if (get_u32(header) != LevelData::kMagic) {
        corrupt("bad magic");
    }
    if (get_u16(header + 4) != LevelData::kFormatVersion) {
        corrupt("unsupported format version " + std::to_string(get_u16(header + 4)));
    }
    if (get_u16(header + 6) != 12) {
        corrupt("unsupported header size " + std::to_string(get_u16(header + 6)));
    }
    const std::uint32_t payload_len = get_u32(header + 8);
    const std::size_t expected_size = 12 + payload_len + 4;
    if (static_cast<std::size_t>(file_size) < expected_size) {
        corrupt("file is " + std::to_string(file_size) + " bytes, payload needs " + std::to_string(expected_size));
    }
    std::vector<std::uint8_t> payload(payload_len);
    in.read(reinterpret_cast<char *>(payload.data()), static_cast<std::streamsize>(payload_len));
    if (!in) {
        corrupt("short read on payload");
    }
    std::uint8_t crc_bytes[4] = {};
    in.read(reinterpret_cast<char *>(crc_bytes), sizeof(crc_bytes));
    if (!in) {
        corrupt("short read on payload checksum");
    }
    if (crc32(payload.data(), payload.size()) != get_u32(crc_bytes)) {
        corrupt("payload checksum mismatch");
    }
    core::ByteBuffer buffer;
    buffer.write_bytes(payload.data(), payload.size());
    buffer.rewind();
    return deserialize_level(buffer);
}

void write_level(const std::filesystem::path &path, const LevelData &level) {
    core::ByteBuffer payload = serialize_level(level);

    std::vector<std::uint8_t> image;
    image.reserve(16 + payload.size() + 4);
    auto push_u32 = [&image](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            image.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    };
    auto push_u16 = [&image](std::uint16_t v) {
        image.push_back(static_cast<std::uint8_t>(v & 0xFF));
        image.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    };
    push_u32(LevelData::kMagic);
    push_u16(LevelData::kFormatVersion);
    push_u16(12); // header size
    push_u32(static_cast<std::uint32_t>(payload.size()));
    image.insert(image.end(), payload.data(), payload.data() + payload.size());
    push_u32(crc32(payload.data(), payload.size()));

    std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    std::filesystem::create_directories(parent);
    const std::filesystem::path temp = path.string() + ".tmp";
    std::FILE *file = std::fopen(temp.c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open temp file " + temp.string());
    }
    try {
        if (std::fwrite(image.data(), 1, image.size(), file) != image.size()) {
            throw std::runtime_error("short write on " + temp.string());
        }
        if (::fflush(file) != 0) {
            throw std::runtime_error("fflush failed");
        }
        if (::fsync(::fileno(file)) != 0) {
            throw std::runtime_error("fsync failed");
        }
        if (std::fclose(file) != 0) {
            file = nullptr;
            throw std::runtime_error("fclose failed");
        }
        file = nullptr;
    } catch (...) {
        if (file != nullptr) {
            std::fclose(file);
        }
        std::filesystem::remove(temp);
        throw;
    }
    std::filesystem::rename(temp, path);
}

} // namespace opencraft::storage
