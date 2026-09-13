#include "opencraft/voxel/chunk.hpp"

#include <stdexcept>
#include <string>

namespace opencraft::voxel {

namespace {

// Local LE helpers for widths the core ByteBuffer does not expose as scalar
// accessors (u16/u64). Everything still travels little-endian, matching the
// ByteBuffer wire convention.
void put_u16(core::ByteBuffer &out, std::uint16_t value) {
    const std::uint8_t bytes[2] = {static_cast<std::uint8_t>(value & 0xFF),
                                   static_cast<std::uint8_t>((value >> 8) & 0xFF)};
    out.write_bytes(bytes, 2);
}

[[nodiscard]] std::uint16_t get_u16(core::ByteBuffer &in) {
    std::uint8_t bytes[2] = {};
    in.read_bytes(bytes, 2);
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[0]) | static_cast<std::uint16_t>(bytes[1]) << 8);
}

void put_u64(core::ByteBuffer &out, std::uint64_t value) {
    std::uint8_t bytes[8] = {};
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
    }
    out.write_bytes(bytes, 8);
}

[[nodiscard]] std::uint64_t get_u64(core::ByteBuffer &in) {
    std::uint8_t bytes[8] = {};
    in.read_bytes(bytes, 8);
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
    }
    return value;
}

[[nodiscard]] int entries_per_word(int bits) {
    return 64 / bits;
}

[[nodiscard]] std::size_t words_needed(int bits) {
    return (Chunk::kSectionVolume * static_cast<std::size_t>(bits) + 63) / 64;
}

} // namespace

// --- PaletteSection ---------------------------------------------------------

std::uint16_t Chunk::PaletteSection::get(int index) const {
    if (uniform_) {
        return uniform_value_;
    }
    const int per_word = entries_per_word(bits_per_entry_);
    const std::uint64_t word = words_[static_cast<std::size_t>(index) / per_word];
    const int shift = (index % per_word) * bits_per_entry_;
    const auto slot = static_cast<std::uint16_t>((word >> shift) & ((1ULL << bits_per_entry_) - 1));
    return palette_[slot];
}

void Chunk::PaletteSection::set(int index, std::uint16_t block_id) {
    if (uniform_) {
        if (block_id == uniform_value_) {
            return;
        }
        materialize();
    }
    const std::uint16_t slot = palette_slot(block_id);
    const int per_word = entries_per_word(bits_per_entry_);
    auto &word = words_[static_cast<std::size_t>(index) / per_word];
    const int shift = (index % per_word) * bits_per_entry_;
    const std::uint64_t mask = (1ULL << bits_per_entry_) - 1;
    word &= ~(mask << shift);
    word |= static_cast<std::uint64_t>(slot) << shift;
}

void Chunk::PaletteSection::materialize() {
    // All 4096 entries equal uniform_value_, stored as palette slot 0.
    palette_ = {uniform_value_};
    bits_per_entry_ = 4;
    words_.assign(words_needed(bits_per_entry_), 0);
    uniform_ = false;
}

std::uint16_t Chunk::PaletteSection::palette_slot(std::uint16_t block_id) {
    for (std::size_t i = 0; i < palette_.size(); ++i) {
        if (palette_[i] == block_id) {
            return static_cast<std::uint16_t>(i);
        }
    }
    const auto capacity = static_cast<std::size_t>(1) << bits_per_entry_;
    if (palette_.size() >= capacity && bits_per_entry_ < kMaxBits) {
        grow_to(bits_per_entry_ * 2);
    }
    // u16 slot indices cap the palette at 0xFFFF entries; the serialized size
    // prefix is u16 as well.
    if (palette_.size() >= 0xFFFF) {
        throw std::overflow_error("section palette exhausted (u16 slots)");
    }
    const auto slot = static_cast<std::uint16_t>(palette_.size());
    palette_.push_back(block_id);
    return slot;
}

void Chunk::PaletteSection::grow_to(int new_bits) {
    std::vector<std::uint64_t> new_words(words_needed(new_bits), 0);
    const int old_per_word = entries_per_word(bits_per_entry_);
    const int new_per_word = entries_per_word(new_bits);
    const std::uint64_t old_mask = (1ULL << bits_per_entry_) - 1;
    for (int index = 0; index < static_cast<int>(kSectionVolume); ++index) {
        const std::uint64_t old_word = words_[static_cast<std::size_t>(index) / old_per_word];
        const int old_shift = (index % old_per_word) * bits_per_entry_;
        const auto slot = static_cast<std::uint64_t>((old_word >> old_shift) & old_mask);
        auto &new_word = new_words[static_cast<std::size_t>(index) / new_per_word];
        const int new_shift = (index % new_per_word) * new_bits;
        new_word |= slot << new_shift;
    }
    words_ = std::move(new_words);
    bits_per_entry_ = new_bits;
}

void Chunk::PaletteSection::serialize(core::ByteBuffer &out) const {
    out.write_u8(static_cast<std::uint8_t>(bits_per_entry_));
    put_u16(out, static_cast<std::uint16_t>(palette_.size()));
    for (const std::uint16_t entry : palette_) {
        put_u16(out, entry);
    }
    if (uniform_) {
        return; // bits == 0: no packed words on the wire
    }
    out.write_u32(static_cast<std::uint32_t>(words_.size()));
    for (const std::uint64_t word : words_) {
        put_u64(out, word);
    }
}

Chunk::PaletteSection Chunk::PaletteSection::deserialize(core::ByteBuffer &in) {
    PaletteSection section;
    const int bits = in.read_u8();
    if (bits != 0 && bits != 4 && bits != 8 && bits != 16) {
        throw std::runtime_error("chunk payload: invalid palette bit width " + std::to_string(bits));
    }
    const std::uint16_t palette_size = get_u16(in);
    if (bits != kMaxBits && palette_size > (1U << bits)) {
        throw std::runtime_error("chunk payload: palette larger than bit width allows");
    }
    section.palette_.resize(palette_size);
    for (std::uint16_t &entry : section.palette_) {
        entry = get_u16(in);
    }
    if (bits == 0) {
        if (section.palette_.size() != 1) {
            throw std::runtime_error("chunk payload: uniform section must have exactly one palette entry");
        }
        section.uniform_ = true;
        section.uniform_value_ = section.palette_[0];
        section.bits_per_entry_ = 0;
        return section;
    }
    section.uniform_ = false;
    section.bits_per_entry_ = bits;
    const std::uint32_t word_count = in.read_u32();
    if (word_count != words_needed(bits)) {
        throw std::runtime_error("chunk payload: unexpected packed word count");
    }
    section.words_.resize(word_count);
    for (std::uint32_t i = 0; i < word_count; ++i) {
        section.words_[i] = get_u64(in);
    }
    return section;
}

// --- Chunk ------------------------------------------------------------------

void Chunk::validate_xyz(int x, int y, int z) {
    if (x < 0 || x >= kSizeX || y < 0 || y >= kSizeY || z < 0 || z >= kSizeZ) {
        throw std::out_of_range("block coordinate out of chunk bounds");
    }
}

int Chunk::local_index(int x, int y_local, int z) {
    return (y_local * kSizeZ + z) * kSizeX + x;
}

std::uint16_t Chunk::get_block(int x, int y, int z) const {
    validate_xyz(x, y, z);
    return sections_[static_cast<std::size_t>(y >> 4)].get(local_index(x, y & 15, z));
}

void Chunk::set_block(int x, int y, int z, std::uint16_t block_id) {
    validate_xyz(x, y, z);
    sections_[static_cast<std::size_t>(y >> 4)].set(local_index(x, y & 15, z), block_id);
}

bool Chunk::section_empty(int section_index) const {
    if (section_index < 0 || section_index >= kSectionCount) {
        throw std::out_of_range("section index out of range");
    }
    return sections_[static_cast<std::size_t>(section_index)].empty();
}

bool Chunk::empty() const {
    for (const PaletteSection &section : sections_) {
        if (!section.empty()) {
            return false;
        }
    }
    return true;
}

Chunk::SectionStats Chunk::section_stats(int section_index) const {
    if (section_index < 0 || section_index >= kSectionCount) {
        throw std::out_of_range("section index out of range");
    }
    const PaletteSection &section = sections_[static_cast<std::size_t>(section_index)];
    return SectionStats{section.uniform(), section.bits_per_entry(), section.palette_size()};
}

std::int64_t Chunk::chunk_coord(int world_x, int world_z) {
    const auto [cx, cz] = chunk_coords(world_x, world_z);
    const auto ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx));
    const auto uz = static_cast<std::uint32_t>(cz);
    return static_cast<std::int64_t>((ux << 32) | uz);
}

std::pair<std::int32_t, std::int32_t> Chunk::chunk_coords(int world_x, int world_z) {
    return {floor_div(world_x, kSizeX), floor_div(world_z, kSizeZ)};
}

std::pair<std::int32_t, std::int32_t> Chunk::unpack_chunk_coord(std::int64_t key) {
    const auto ukey = static_cast<std::uint64_t>(key);
    const auto cx = static_cast<std::int32_t>(static_cast<std::uint32_t>(ukey >> 32));
    const auto cz = static_cast<std::int32_t>(static_cast<std::uint32_t>(ukey & 0xFFFFFFFFULL));
    return {cx, cz};
}

int Chunk::floor_div(int value, int divisor) {
    int quotient = value / divisor;
    if ((value % divisor != 0) && ((value < 0) != (divisor < 0))) {
        --quotient;
    }
    return quotient;
}

void Chunk::serialize(core::ByteBuffer &out) const {
    out.write_version(kFormatVersion);
    std::uint8_t non_empty = 0;
    for (const PaletteSection &section : sections_) {
        if (!section.empty()) {
            ++non_empty;
        }
    }
    out.write_u8(non_empty);
    for (std::size_t i = 0; i < sections_.size(); ++i) {
        if (sections_[i].empty()) {
            continue;
        }
        out.write_u8(static_cast<std::uint8_t>(i));
        sections_[i].serialize(out);
    }
}

Chunk Chunk::deserialize(core::ByteBuffer &in) {
    static_cast<void>(in.read_version(kFormatVersion));
    Chunk chunk;
    const std::uint8_t non_empty = in.read_u8();
    if (non_empty > kSectionCount) {
        throw std::runtime_error("chunk payload: non-empty section count exceeds chunk size");
    }
    for (std::uint8_t i = 0; i < non_empty; ++i) {
        const std::uint8_t index = in.read_u8();
        if (index >= kSectionCount) {
            throw std::runtime_error("chunk payload: section index out of range");
        }
        chunk.sections_[index] = PaletteSection::deserialize(in);
    }
    return chunk;
}

} // namespace opencraft::voxel
