#include "opencraft/core/byte_buffer.hpp"

#include <cstring>
#include <stdexcept>

namespace opencraft::core {

void ByteBuffer::write_u8(std::uint8_t value) {
    data_.push_back(value);
}

void ByteBuffer::write_u32(std::uint32_t value) {
    detail::put_le(data_, value);
}

void ByteBuffer::write_float(float value) {
    detail::put_le(data_, value);
}

void ByteBuffer::write_bytes(const std::uint8_t *bytes, std::size_t size) {
    data_.insert(data_.end(), bytes, bytes + size);
}

void ByteBuffer::write_version(std::uint32_t version) {
    write_u32(version);
}

void ByteBuffer::write_string(std::string_view value) {
    write_u32(static_cast<std::uint32_t>(value.size()));
    data_.insert(data_.end(), value.begin(), value.end());
}

std::uint8_t ByteBuffer::read_u8() {
    ensure_readable(1);
    return data_[read_pos_++];
}

std::uint32_t ByteBuffer::read_u32() {
    ensure_readable(4);
    const std::uint32_t value = detail::get_le<std::uint32_t>(data_, read_pos_);
    read_pos_ += 4;
    return value;
}

float ByteBuffer::read_float() {
    ensure_readable(4);
    const float value = detail::get_le<float>(data_, read_pos_);
    read_pos_ += 4;
    return value;
}

void ByteBuffer::read_bytes(std::uint8_t *out, std::size_t size) {
    ensure_readable(size);
    std::memcpy(out, data_.data() + read_pos_, size);
    read_pos_ += size;
}

std::uint32_t ByteBuffer::read_version() {
    return read_u32();
}

std::uint32_t ByteBuffer::read_version(std::uint32_t expected) {
    const std::uint32_t actual = read_u32();
    if (actual != expected) {
        throw std::runtime_error("ByteBuffer version mismatch: expected " + std::to_string(expected) + ", got " +
                                 std::to_string(actual));
    }
    return actual;
}

std::string ByteBuffer::read_string() {
    const std::uint32_t length = read_u32();
    ensure_readable(length);
    std::string value(reinterpret_cast<const char *>(data_.data() + read_pos_), length);
    read_pos_ += length;
    return value;
}

void ByteBuffer::ensure_readable(std::size_t count) const {
    if (remaining() < count) {
        throw std::out_of_range("ByteBuffer underflow");
    }
}

} // namespace opencraft::core
