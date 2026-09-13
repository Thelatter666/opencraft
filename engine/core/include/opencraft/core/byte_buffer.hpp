#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace opencraft::core {

// Minimal growable byte buffer with positional read/write, little-endian.
// Seed for the serialization layer (docs/03 §1 engine/core); kept small on purpose.
class ByteBuffer {
public:
    void write_u8(std::uint8_t value);
    void write_u32(std::uint32_t value);
    void write_float(float value);
    void write_bytes(const std::uint8_t *data, std::size_t size);

    [[nodiscard]] std::uint8_t read_u8();
    [[nodiscard]] std::uint32_t read_u32();
    [[nodiscard]] float read_float();

    void read_bytes(std::uint8_t *out, std::size_t size);

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
