#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace opencraft::core {

// Little-endian encoding helpers shared by the inline ByteBuffer templates.
// Floats are bit-copied to an unsigned integer of the same width first, so
// every type travels as a plain little-endian integer on the wire.
namespace detail {

template <typename T>
using UnsignedBits =
    std::conditional_t<sizeof(T) == 1, std::uint8_t,
                       std::conditional_t<sizeof(T) == 2, std::uint16_t,
                                          std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;

template <typename T>
inline void put_le(std::vector<std::uint8_t> &out, T value) {
    static_assert(std::is_arithmetic_v<T>);
    const auto bits = static_cast<UnsignedBits<T>>([&value] {
        if constexpr (std::is_floating_point_v<T>) {
            UnsignedBits<T> bits_copy = 0;
            std::memcpy(&bits_copy, &value, sizeof(T));
            return bits_copy;
        } else {
            return value;
        }
    }());
    std::uint8_t bytes[sizeof(UnsignedBits<T>)] = {};
    for (std::size_t i = 0; i < sizeof(UnsignedBits<T>); ++i) {
        bytes[i] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF);
    }
    out.insert(out.end(), bytes, bytes + sizeof(UnsignedBits<T>));
}

template <typename T>
inline T get_le(const std::vector<std::uint8_t> &in, std::size_t pos) {
    static_assert(std::is_arithmetic_v<T>);
    auto bits = UnsignedBits<T>{0};
    for (std::size_t i = 0; i < sizeof(UnsignedBits<T>); ++i) {
        bits |= static_cast<UnsignedBits<T>>(in[pos + i]) << (8 * i);
    }
    if constexpr (std::is_floating_point_v<T>) {
        T value = T{0};
        std::memcpy(&value, &bits, sizeof(T));
        return value;
    } else {
        return static_cast<T>(bits);
    }
}

} // namespace detail

// Growable byte buffer with positional read/write, little-endian on the wire.
// Reading past the consumed range throws std::out_of_range; version mismatches
// throw std::runtime_error. Seed for the serialization layer (docs/03 §1).
class ByteBuffer {
public:
    // --- fixed-width scalars -------------------------------------------------
    void write_u8(std::uint8_t value);
    void write_u16(std::uint16_t value);
    void write_u32(std::uint32_t value);
    void write_u64(std::uint64_t value);
    void write_float(float value);
    void write_bytes(const std::uint8_t *data, std::size_t size);

    [[nodiscard]] std::uint8_t read_u8();
    [[nodiscard]] std::uint16_t read_u16();
    [[nodiscard]] std::uint32_t read_u32();
    [[nodiscard]] std::uint64_t read_u64();
    [[nodiscard]] float read_float();

    void read_bytes(std::uint8_t *out, std::size_t size);

    // --- versioned payload helpers -------------------------------------------
    // A versioned payload section starts with a u32 version prefix. Readers
    // either accept whatever version is present (read_version()) or demand a
    // specific one (read_version(expected), throws on mismatch).
    void write_version(std::uint32_t version);
    [[nodiscard]] std::uint32_t read_version();
    [[nodiscard]] std::uint32_t read_version(std::uint32_t expected);

    // --- strings and containers ----------------------------------------------
    // u32 byte-length prefix + raw bytes (no null terminator).
    void write_string(std::string_view value);
    [[nodiscard]] std::string read_string();

    // u32 count prefix + little-endian elements. Works for any arithmetic
    // element type, including float/double (bit-copied, see detail::put_le).
    template <typename T>
    void write_vector(const std::vector<T> &values) {
        static_assert(std::is_arithmetic_v<T>);
        write_u32(static_cast<std::uint32_t>(values.size()));
        for (const T &value : values) {
            detail::put_le(data_, value);
        }
    }

    template <typename T>
    [[nodiscard]] std::vector<T> read_vector() {
        static_assert(std::is_arithmetic_v<T>);
        const std::uint32_t count = read_u32();
        std::vector<T> values;
        values.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            ensure_readable(sizeof(T));
            values.push_back(detail::get_le<T>(data_, read_pos_));
            read_pos_ += sizeof(T);
        }
        return values;
    }

    // --- position helpers ------------------------------------------------------
    void rewind() { read_pos_ = 0; }

    [[nodiscard]] std::size_t size() const { return data_.size(); }

    [[nodiscard]] std::size_t remaining() const { return data_.size() - read_pos_; }

    [[nodiscard]] const std::uint8_t *data() const { return data_.data(); }

private:
    void ensure_readable(std::size_t count) const;

    std::vector<std::uint8_t> data_;
    std::size_t read_pos_ = 0;
};

} // namespace opencraft::core
