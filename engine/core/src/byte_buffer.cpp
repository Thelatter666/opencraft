#include "opencraft/core/byte_buffer.hpp"

#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace opencraft::core {

namespace {

template <typename T>
void put_trivial(std::vector<std::uint8_t> &out, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::uint8_t bytes[sizeof(T)] = {};
    // Little-endian regardless of host byte order.
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
    }
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <typename T>
T get_trivial(const std::vector<std::uint8_t> &in, std::size_t &pos) {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(in.at(pos + i)) << (8 * i);
    }
    pos += sizeof(T);
    return value;
}

} // namespace

void ByteBuffer::write_u8(std::uint8_t value) {
    data_.push_back(value);
}

void ByteBuffer::write_u32(std::uint32_t value) {
    put_trivial(data_, value);
}

void ByteBuffer::write_float(float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_u32(bits);
}

void ByteBuffer::write_bytes(const std::uint8_t *bytes, std::size_t size) {
    data_.insert(data_.end(), bytes, bytes + size);
}

std::uint8_t ByteBuffer::read_u8() {
    ensure_readable(1);
    return data_[read_pos_++];
}

std::uint32_t ByteBuffer::read_u32() {
    ensure_readable(4);
    return get_trivial<std::uint32_t>(data_, read_pos_);
}

float ByteBuffer::read_float() {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    const std::uint32_t bits = read_u32();
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void ByteBuffer::read_bytes(std::uint8_t *out, std::size_t size) {
    ensure_readable(size);
    std::memcpy(out, data_.data() + read_pos_, size);
    read_pos_ += size;
}

void ByteBuffer::ensure_readable(std::size_t count) const {
    if (remaining() < count) {
        throw std::out_of_range("ByteBuffer underflow");
    }
}

} // namespace opencraft::core
