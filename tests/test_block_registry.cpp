#include <stdexcept>

#include <doctest/doctest.h>

#include "opencraft/voxel/block_registry.hpp"

TEST_CASE("registry reserves air as id zero") {
    const auto registry = opencraft::voxel::BlockRegistry::create_default();
    CHECK(registry.air() == 0);
    CHECK(registry.find_id("air").value_or(99) == 0);
    const auto &def = registry.def_of(0);
    CHECK_FALSE(def.solid);
    CHECK(def.transparent);
}

TEST_CASE("registry assigns dense numeric ids and supports both query directions") {
    auto registry = opencraft::voxel::BlockRegistry::create_default();
    const auto first_new = registry.register_block("test_a", {"Test A", true, false, 1.0F});
    const auto second_new = registry.register_block("test_b", {"Test B", false, true, 0.0F});
    CHECK(first_new == registry.size() - 2);
    CHECK(second_new == first_new + 1);

    CHECK(registry.find_id("test_a") == first_new);
    CHECK(registry.id_of("test_b") == second_new);
    CHECK(registry.string_of(first_new) == "test_a");
    CHECK(registry.string_of(second_new) == "test_b");

    const auto &stone = registry.def_of(registry.id_of("stone"));
    CHECK(stone.display_name == "Stone");
    CHECK(stone.solid);
    CHECK_FALSE(stone.transparent);
    CHECK(stone.hardness == doctest::Approx(1.5F));

    // Water must be non-solid yet non-air: the two flags stay independent.
    const auto &water = registry.def_of(registry.id_of("water"));
    CHECK_FALSE(water.solid);
    CHECK(water.transparent);

    CHECK(registry.has_id("dirt"));
    CHECK_FALSE(registry.has_id("not_a_block"));
}

TEST_CASE("registry default set covers the launch blocks with hardness values") {
    const auto registry = opencraft::voxel::BlockRegistry::create_default();
    CHECK(registry.size() >= 20);
    CHECK(registry.def_of(registry.id_of("bedrock")).hardness < 0.0F);
    CHECK(registry.def_of(registry.id_of("obsidian")).hardness == doctest::Approx(50.0F));
    CHECK(registry.def_of(registry.id_of("log")).hardness == doctest::Approx(2.0F));
    CHECK(registry.def_of(registry.id_of("diamond_ore")).hardness == doctest::Approx(3.0F));
}

TEST_CASE("registry rejects duplicates and empty ids") {
    auto registry = opencraft::voxel::BlockRegistry::create_default();
    CHECK_THROWS_AS(registry.register_block("stone", {"Dup", true, false, 1.0F}), std::invalid_argument);
    CHECK_THROWS_AS(registry.register_block("", {"Empty", true, false, 1.0F}), std::invalid_argument);
    // ★ T-D60: 22 - air plus the twenty launch blocks plus `assembly_bench`.
    CHECK(registry.size() == 22); // unchanged by the failed registrations above
}

TEST_CASE("registry handles unknown ids on both directions") {
    const auto registry = opencraft::voxel::BlockRegistry::create_default();
    CHECK_FALSE(registry.find_id("missing").has_value());
    CHECK_THROWS_AS([&] { static_cast<void>(registry.id_of("missing")); }(), std::out_of_range);
    CHECK_FALSE(registry.has_numeric(9999));
    CHECK_THROWS_AS([&] { static_cast<void>(registry.string_of(9999)); }(), std::out_of_range);
    CHECK_THROWS_AS([&] { static_cast<void>(registry.def_of(9999)); }(), std::out_of_range);
}

TEST_CASE("water is the default liquid and every other block defaults to non-liquid") {
    const auto registry = opencraft::voxel::BlockRegistry::create_default();
    CHECK(registry.def_of(registry.id_of("water")).liquid);
    CHECK_FALSE(registry.def_of(registry.id_of("water")).solid); // semantics unchanged
    for (std::uint16_t id = 0; id < registry.size(); ++id) {
        if (registry.string_of(id) != "water") {
            CHECK_FALSE(registry.def_of(id).liquid);
        }
    }
    // New registrations default to liquid = false.
    auto custom = opencraft::voxel::BlockRegistry::create_default();
    const auto id = custom.register_block("magma_slurry", {"Magma Slurry", false, true, 1.0F});
    CHECK_FALSE(custom.def_of(id).liquid);
}
