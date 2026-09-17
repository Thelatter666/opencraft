// T-E1: the entity layer's two halves - the type registry (game/entity_type.hpp)
// and the storage (server/entity_store.hpp).
//
// The card's acceptance item 1 is "有明确的实体类型体系与存储；遍历确定顺序
// （可断言）", so the iteration-order assertion is the core of this file: the
// order is a property of the container, not of a call site, and it survives
// erasure and slot reuse.

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "opencraft/game/entity_type.hpp"
#include "opencraft/sim/entity_store.hpp"
#include "opencraft/sim/item_sim.hpp"

namespace {

namespace gam = opencraft::game;
namespace srv = opencraft::server;

[[nodiscard]] srv::Entity item_at(double x, double y, double z, std::uint16_t type) {
    srv::Entity entity;
    entity.type = type;
    entity.position = {x, y, z};
    return entity;
}

// Ascending id order == ascending slot order == the order for_each_entity
// visits. Collected through the public visitor, so this is what a consumer sees.
[[nodiscard]] std::vector<srv::EntityId> visited_ids(srv::EntityStore &store) {
    std::vector<srv::EntityId> ids;
    store.for_each_entity([&](const srv::Entity &entity) { ids.push_back(entity.id); });
    return ids;
}

} // namespace

TEST_CASE("entity type registry: the launch set is the dropped item with research/11 §4.1-4.4 numbers") {
    const gam::EntityTypeRegistry types = gam::EntityTypeRegistry::create_default();
    CHECK(types.size() == 2); // the reserved empty entry + the launch type
    CHECK(types.string_of(gam::EntityTypeRegistry::kEmptyId) == "empty");

    const std::uint16_t item = types.id_of("item");
    const gam::EntityDef &def = types.def_of(item);
    // ⚖ research/11 §4.2: a 0.25 x 0.25 x 0.25 collision box.
    CHECK(def.half_width == doctest::Approx(0.125));
    CHECK(def.height == doctest::Approx(0.25));
    // ⚖ research/11 §4.1: gravity -0.04 (NOT the player's -0.08) and a 0.98
    // horizontal drag (NOT the player's 0.91). Both are asserted against the
    // player's own values below, because "not the player's" is the whole point
    // of having per-type parameters (docs/03 §6).
    CHECK(def.gravity == doctest::Approx(0.04));
    CHECK(def.vertical_drag == doctest::Approx(0.98));
    CHECK(def.horizontal_drag == doctest::Approx(0.98));
    CHECK(def.max_health == 5); // ⚖ research/11 §4.4
    CHECK(def.carries_item_stack);
}

TEST_CASE("entity type registry: ids are dense, lookups are total and duplicates throw") {
    gam::EntityTypeRegistry types;
    CHECK(types.size() == 1); // the reserved entry only
    CHECK(types.has_numeric(gam::EntityTypeRegistry::kEmptyId));
    CHECK_FALSE(types.has_id("item"));

    const std::uint16_t first = types.register_type("item", {});
    const std::uint16_t second = types.register_type("mob", {});
    CHECK(first == 1);
    CHECK(second == 2);
    CHECK(types.id_of("item") == first);
    CHECK(types.string_of(second) == "mob");
    CHECK(types.find_id("nothing").has_value() == false);

    CHECK_THROWS_AS(types.register_type("item", {}), std::invalid_argument);
    CHECK_THROWS_AS(types.register_type("", {}), std::invalid_argument);
    CHECK_THROWS_AS([&] { static_cast<void>(types.def_of(99)); }(), std::out_of_range);
    CHECK_THROWS_AS([&] { static_cast<void>(types.id_of("nothing")); }(), std::out_of_range);

    // A degenerate collision box is refused: every consumer (collision, pickup,
    // render) would silently misbehave with one.
    gam::EntityDef flat;
    flat.half_width = 0.0;
    CHECK_THROWS_AS(types.register_type("flat", flat), std::invalid_argument);
}

TEST_CASE("entity store: iteration is ascending id, and that order survives erasure and reuse") {
    const gam::EntityTypeRegistry types = gam::EntityTypeRegistry::create_default();
    const std::uint16_t item = types.id_of("item");
    srv::EntityStore store;

    const srv::EntityId a = store.spawn(item_at(0.0, 8.0, 0.0, item));
    const srv::EntityId b = store.spawn(item_at(1.0, 8.0, 0.0, item));
    const srv::EntityId c = store.spawn(item_at(2.0, 8.0, 0.0, item));
    CHECK(a == 1);
    CHECK(b == 2);
    CHECK(c == 3);
    CHECK(store.alive_count() == 3);
    CHECK(visited_ids(store) == std::vector<srv::EntityId>{a, b, c});
    CHECK(store.live_ids() == std::vector<srv::EntityId>{a, b, c});

    // Erase the middle: the visit order is still ascending, with a hole.
    CHECK(store.erase(b));
    CHECK_FALSE(store.erase(b)); // already gone
    CHECK(store.find(b) == nullptr);
    CHECK(store.alive_count() == 2);
    CHECK(visited_ids(store) == std::vector<srv::EntityId>{a, c});

    // The freed slot is reused (LIFO), so the new entity takes the LOW id - the
    // order is deterministic for a given operation sequence, not "spawn order".
    const srv::EntityId d = store.spawn(item_at(3.0, 8.0, 0.0, item));
    CHECK(d == b);
    CHECK(visited_ids(store) == std::vector<srv::EntityId>{a, d, c});

    // Id 0 is never handed out, and it is never live.
    CHECK(srv::EntityStore::kNoEntity == 0);
    CHECK(store.find(0) == nullptr);
    CHECK_FALSE(store.erase(0));
    CHECK_FALSE(store.erase(999));

    store.clear();
    CHECK(store.alive_count() == 0);
    CHECK(store.live_ids().empty());
}

TEST_CASE("entity store: spawn stamps the id it returned, and erase during a visit is safe") {
    const gam::EntityTypeRegistry types = gam::EntityTypeRegistry::create_default();
    const std::uint16_t item = types.id_of("item");
    srv::EntityStore store;
    const srv::EntityId a = store.spawn(item_at(0.0, 8.0, 0.0, item));
    const srv::EntityId b = store.spawn(item_at(1.0, 8.0, 0.0, item));
    CHECK(store.find(a)->id == a);
    CHECK(store.find(b)->id == b);

    // The simulation erases inside the visitor (despawn, hazard, merge), which
    // is exactly what this asserts is safe.
    store.for_each_entity([&](srv::Entity &entity) {
        if (entity.id == a) {
            store.erase(a);
        }
    });
    CHECK(store.alive_count() == 1);
    CHECK(store.find(b) != nullptr);
    CHECK(store.find(b)->position.x == doctest::Approx(1.0));
}
