#include <doctest/doctest.h>

#include "opencraft/core/math.hpp"

using glm::vec3;
using opencraft::core::AABB;

TEST_CASE("AABB overlap detection") {
    const AABB a{vec3(0.0F, 0.0F, 0.0F), vec3(2.0F, 2.0F, 2.0F)};
    const AABB shifted{vec3(1.0F, 1.0F, 1.0F), vec3(3.0F, 3.0F, 3.0F)};
    const AABB separated{vec3(5.0F, 5.0F, 5.0F), vec3(6.0F, 6.0F, 6.0F)};
    const AABB face_touching{vec3(2.0F, 0.0F, 0.0F), vec3(4.0F, 2.0F, 2.0F)};

    CHECK(a.overlaps(a)); // self-overlap
    CHECK(a.overlaps(shifted));
    CHECK_FALSE(a.overlaps(separated));
    CHECK_FALSE(a.overlaps(face_touching)); // exclusive max: face contact is not overlap
}

TEST_CASE("AABB point containment") {
    const AABB box{vec3(-1.0F, 0.0F, -1.0F), vec3(1.0F, 2.0F, 1.0F)};

    CHECK(box.contains(vec3(0.0F, 0.0F, 0.0F)));
    CHECK(box.contains(vec3(-1.0F, 0.0F, -1.0F)));     // min corner is inclusive
    CHECK_FALSE(box.contains(vec3(1.0F, 1.0F, 0.0F))); // max corner is exclusive
    CHECK_FALSE(box.contains(vec3(2.0F, 0.0F, 0.0F)));
}

TEST_CASE("AABB containment of another box") {
    const AABB outer{vec3(0.0F, 0.0F, 0.0F), vec3(16.0F, 16.0F, 16.0F)};
    const AABB inner{vec3(4.0F, 4.0F, 4.0F), vec3(8.0F, 8.0F, 8.0F)};
    const AABB poking_out{vec3(4.0F, 4.0F, 4.0F), vec3(20.0F, 8.0F, 8.0F)};

    CHECK(outer.contains(inner));
    CHECK(outer.contains(outer));
    CHECK_FALSE(outer.contains(poking_out));
    CHECK_FALSE(inner.contains(outer));
}

TEST_CASE("AABB expansion, size and center") {
    const AABB box{vec3(1.0F, 1.0F, 1.0F), vec3(3.0F, 5.0F, 7.0F)};

    const AABB grown = box.expanded(0.5F);
    CHECK(grown.min == vec3(0.5F, 0.5F, 0.5F));
    CHECK(grown.max == vec3(3.5F, 5.5F, 7.5F));
    CHECK(box.expanded(-0.5F).min == vec3(1.5F, 1.5F, 1.5F));

    CHECK(box.size() == vec3(2.0F, 4.0F, 6.0F));
    CHECK(box.center() == vec3(2.0F, 3.0F, 4.0F));

    // Expanding until boxes overlap makes overlap detection succeed.
    CHECK_FALSE(box.overlaps(AABB{vec3(10.0F, 10.0F, 10.0F), vec3(12.0F, 12.0F, 12.0F)}));
    CHECK(box.expanded(7.5F).overlaps(AABB{vec3(10.0F, 10.0F, 10.0F), vec3(12.0F, 12.0F, 12.0F)}));
}
