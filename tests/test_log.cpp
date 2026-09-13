#include <doctest/doctest.h>

#include "opencraft/core/log.hpp"

TEST_CASE("log init is idempotent and the macros link") {
    // spdlog::default_logger() exists even before init(), so calling init()
    // twice must not crash, recreate, or otherwise misbehave.
    opencraft::core::log::init();
    opencraft::core::log::init();

    OC_LOG_INFO("test log line: {}", 42); // must compile and link without side effects

    opencraft::core::log::set_level(spdlog::level::off);
    OC_LOG_ERROR("filtered out at runtime, still must compile");
    opencraft::core::log::set_level(spdlog::level::info);

    CHECK(true);
}
