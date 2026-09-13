#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "opencraft/core/byte_buffer.hpp"

namespace opencraft::voxel {

// A 16x384x16 voxel chunk made of 24 cubic 16^3 sections (docs/02 §1,
// docs/03 §7). Storage details (per-section palette + bit packing) are hidden
// inside the class so the backing layout can change without touching callers.
//
// Section-internal layout is YZX (y major, z middle, x minor) per
// docs/research/02 §1.1: index = (y_local * 16 + z) * 16 + x.
//
// Palette growth path: uniform (single value, 0 bits) -> 4 bits -> 8 bits ->
// 16 bits. Once a section becomes non-uniform it never shrinks back: palette
// entries and bit width are kept even if every block is set back to its
// original value. This is the documented trade-off (kept in favor of
// simplicity); shrinking may be added later behind the same interface.
class Chunk {
public:
    static constexpr int kSizeX = 16;
    static constexpr int kSizeY = 384;
    static constexpr int kSizeZ = 16;
    static constexpr int kSectionSize = 16;
    static constexpr int kSectionCount = kSizeY / kSectionSize; // 24
    static constexpr std::size_t kSectionVolume =
        static_cast<std::size_t>(kSectionSize) * kSectionSize * kSectionSize; // 4096

    static constexpr std::uint32_t kFormatVersion = 1;

    Chunk() = default;

    // --- block access --------------------------------------------------------
    // Local chunk coordinates; throws std::out_of_range when out of bounds.
    [[nodiscard]] std::uint16_t get_block(int x, int y, int z) const;
    void set_block(int x, int y, int z, std::uint16_t block_id);

    // A section is empty while it holds a single value equal to air (id 0).
    // Throws std::out_of_range for an invalid section index.
    [[nodiscard]] bool section_empty(int section_index) const;

    [[nodiscard]] bool empty() const;

    // Read-only introspection for tests/diagnostics; not part of the gameplay
    // interface and safe to ignore by consumers.
    struct SectionStats {
        bool uniform = true;
        int bits_per_entry = 0;
        std::size_t palette_size = 0;
    };

    [[nodiscard]] SectionStats section_stats(int section_index) const;

    // --- coordinate helpers --------------------------------------------------
    // World block coordinates -> packed chunk coordinate key. Uses floor
    // division, so negative world coordinates map to negative chunk
    // coordinates correctly (e.g. x = -1 -> cx = -1). The key packs (cx, cz)
    // as two i32 halves and is suitable as a hash-map key.
    [[nodiscard]] static std::int64_t chunk_coord(int world_x, int world_z);

    // Same computation as chunk_coord but returns the pair explicitly.
    [[nodiscard]] static std::pair<std::int32_t, std::int32_t> chunk_coords(int world_x, int world_z);

    // Inverse of chunk_coord.
    [[nodiscard]] static std::pair<std::int32_t, std::int32_t> unpack_chunk_coord(std::int64_t key);

    // Floor division valid for any sign combination.
    [[nodiscard]] static int floor_div(int value, int divisor);

    // --- serialization -------------------------------------------------------
    // Format v1: version prefix, u8 count of non-empty sections, then per
    // section: u8 section index, u8 bits (0 = uniform, 4, 8 or 16), u16
    // palette size + palette entries (u16 each), and for bits > 0 a u32 word
    // count + packed words (u64 each, entries packed LSB-first). Empty
    // sections occupy nothing on the wire.
    void serialize(core::ByteBuffer &out) const;

    // Reads a payload produced by serialize() (version-checked) and replaces
    // all chunk content. Throws std::runtime_error on a malformed payload.
    [[nodiscard]] static Chunk deserialize(core::ByteBuffer &in);

private:
    class PaletteSection {
    public:
        [[nodiscard]] std::uint16_t get(int index) const;
        void set(int index, std::uint16_t block_id);

        [[nodiscard]] bool empty() const { return uniform_ && uniform_value_ == 0; }

        [[nodiscard]] bool uniform() const { return uniform_; }

        [[nodiscard]] int bits_per_entry() const { return bits_per_entry_; }

        [[nodiscard]] std::size_t palette_size() const { return palette_.size(); }

        void serialize(core::ByteBuffer &out) const;
        // Reads exactly one section payload (without the section index byte).
        static PaletteSection deserialize(core::ByteBuffer &in);

    private:
        static constexpr int kMaxBits = 16;

        void materialize();
        void grow_to(int new_bits);
        // Returns the palette slot of block_id, appending (and growing the
        // bit width if needed) when the value is not present yet.
        std::uint16_t palette_slot(std::uint16_t block_id);

        bool uniform_ = true;
        std::uint16_t uniform_value_ = 0;
        std::vector<std::uint16_t> palette_;
        std::vector<std::uint64_t> words_;
        int bits_per_entry_ = 0;
    };

    static void validate_xyz(int x, int y, int z);
    // YZX order inside a section: (y_local * 16 + z) * 16 + x.
    [[nodiscard]] static int local_index(int x, int y_local, int z);

    std::array<PaletteSection, kSectionCount> sections_;
};

} // namespace opencraft::voxel
