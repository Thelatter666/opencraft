#include <doctest/doctest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "opencraft/core/byte_buffer.hpp"

using opencraft::core::ByteBuffer;

TEST_CASE("versioned payload round-trips and rejects wrong versions") {
    ByteBuffer buf;
    buf.write_version(3);
    buf.write_u32(42);

    buf.rewind();
    CHECK(buf.read_version(3) == 3);
    CHECK(buf.read_u32() == 42);

    // Plain read reveals whatever version prefix is present.
    ByteBuffer other;
    other.write_version(7);
    other.rewind();
    CHECK(other.read_version() == 7);

    ByteBuffer stale;
    stale.write_version(2);
    stale.rewind();
    CHECK_THROWS_AS(static_cast<void>(stale.read_version(3)), std::runtime_error);
}

TEST_CASE("versioned payload with string and vector members round-trips") {
    ByteBuffer buf;
    buf.write_version(1);
    buf.write_string("chunk:overworld:0,-1");
    const std::vector<std::uint16_t> blocks = {0, 1, 65535, 42};
    buf.write_vector(blocks);
    const std::vector<float> heights = {1.5F, -0.25F, 63.0F};
    buf.write_vector(heights);

    buf.rewind();
    CHECK(buf.read_version(1) == 1);
    CHECK(buf.read_string() == "chunk:overworld:0,-1");
    CHECK((buf.read_vector<std::uint16_t>() == blocks));
    const std::vector<float> read_heights = buf.read_vector<float>();
    REQUIRE(read_heights.size() == heights.size());
    for (std::size_t i = 0; i < heights.size(); ++i) {
        CHECK(read_heights[i] == doctest::Approx(heights[i]));
    }
    CHECK(buf.remaining() == 0);
}

TEST_CASE("string and container helpers preserve the existing wire semantics") {
    ByteBuffer buf;
    buf.write_string("");
    buf.write_string("A");

    // Length prefixes are plain little-endian u32 at the expected offsets.
    CHECK(buf.data()[0] == 0);
    CHECK(buf.data()[3] == 0);
    CHECK(buf.data()[4] == 1);
    CHECK(buf.data()[7] == 0);

    buf.rewind();
    CHECK(buf.read_string().empty());
    CHECK(buf.read_string() == "A");
    CHECK_THROWS_AS(static_cast<void>(buf.read_string()), std::out_of_range);

    ByteBuffer truncated_vector;
    truncated_vector.write_u32(3); // claims 3 elements
    truncated_vector.write_u8(1);  // but only 1 byte of payload
    truncated_vector.rewind();
    CHECK_THROWS_AS(static_cast<void>(truncated_vector.read_vector<std::uint32_t>()), std::out_of_range);
}

TEST_CASE("existing scalar round-trip still works alongside the new helpers") {
    ByteBuffer buf;
    buf.write_float(-2.5F);
    buf.write_vector(std::vector<double>{3.14159});
    buf.rewind();
    CHECK(buf.read_float() == doctest::Approx(-2.5F));
    CHECK(buf.read_vector<double>()[0] == doctest::Approx(3.14159));
}
