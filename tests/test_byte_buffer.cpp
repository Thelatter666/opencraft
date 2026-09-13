#include <doctest/doctest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "opencraft/core/byte_buffer.hpp"

using opencraft::core::ByteBuffer;

TEST_CASE("write then read round-trips values little-endian") {
    ByteBuffer buf;
    buf.write_u8(0xAB);
    buf.write_u32(0x12345678);
    buf.write_float(3.5F);
    buf.write_bytes(reinterpret_cast<const std::uint8_t *>("oc"), 2);

    // Little-endian layout on the wire, independent of host byte order.
    const std::uint8_t *raw = buf.data();
    CHECK(raw[1] == 0x78);
    CHECK(raw[2] == 0x56);
    CHECK(raw[3] == 0x34);
    CHECK(raw[4] == 0x12);

    buf.rewind();
    CHECK(buf.remaining() == 11);
    CHECK(buf.read_u8() == 0xAB);
    CHECK(buf.read_u32() == 0x12345678);
    CHECK(buf.read_float() == 3.5F);

    std::uint8_t tail[2] = {};
    buf.read_bytes(tail, 2);
    CHECK(tail[0] == 'o');
    CHECK(tail[1] == 'c');
    CHECK(buf.remaining() == 0);
}

TEST_CASE("reading past the end throws") {
    ByteBuffer buf;
    buf.write_u8(1);
    buf.rewind();
    CHECK_THROWS_AS(static_cast<void>(buf.read_u32()), std::out_of_range);
}
