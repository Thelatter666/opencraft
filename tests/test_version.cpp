#include <doctest/doctest.h>

#include <string>

#include "opencraft/core/version.hpp"

TEST_CASE("version constants are consistent with version_string") {
    using namespace opencraft::core;

    CHECK(kVersionMajor == 0);
    CHECK(kVersionMinor == 1);
    CHECK(kVersionPatch == 0);
    CHECK(kProjectName == "OpenCraft");
    CHECK(version_string() == "OpenCraft 0.1.0");
}
