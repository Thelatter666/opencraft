#pragma once

// T-M2: the hand-built block world and the fixture the three mob test files
// share (test_mob_goal.cpp / test_mob_ai.cpp / test_mob_spawn.cpp), in the
// spirit of physics_test_world.hpp.
//
// The point of building the world here rather than reaching for WorldSim is the
// same one item_sim's tests make: the rules are pure functions of (store, world,
// registries, rules), so they can be driven against a floor, a wall and a light
// level with no chunks, no worldgen, no client and no window - and an assertion
// about "a mob notices you at 35 blocks" then measures exactly that, not the
// terrain generator's opinion of the spawn area.

#include <cstdint>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "opencraft/game/entity_type.hpp"
#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/mob_type.hpp"
#include "opencraft/physics/player_state.hpp"
#include "opencraft/sim/entity_store.hpp"
#include "opencraft/sim/mob_sim.hpp"
#include "opencraft/sim/mob_spawn.hpp"
#include "opencraft/voxel/block_registry.hpp"

namespace mobtest {

namespace gam = opencraft::game;
namespace srv = opencraft::server;

// A block world for the mob rules: inclusive integer regions carrying a block id
// (0 = air), a light level per region, and a set of chunk columns that are NOT in
// memory. Same shape as the drop tests' BlockWorld, plus the light query the
// spawner needs.
class MobTestWorld final : public srv::IMobWorld {
public:
    MobTestWorld &fill(int x0, int x1, int y0, int y1, int z0, int z1, std::uint16_t block) {
        blocks_.push_back({x0, x1, y0, y1, z0, z1, block});
        return *this;
    }

    // A solid floor whose TOP FACE is at `top_y`: a standing body's feet are at
    // `top_y`, which is where every test puts the mobs and the actor.
    MobTestWorld &floor(int top_y, std::uint16_t block, int half_extent = 200) {
        return fill(-half_extent, half_extent, top_y - 64, top_y - 1, -half_extent, half_extent, block);
    }

    // Light level of a region. Regions are scanned in order, last match wins, and
    // the default everywhere is 15 (daylight) - so a spawn test says "make this
    // room dark" rather than "make the world bright".
    MobTestWorld &light(int x0, int x1, int y0, int y1, int z0, int z1, int level) {
        lights_.push_back({x0, x1, y0, y1, z0, z1, level});
        return *this;
    }

    MobTestWorld &light_everywhere(int level) { return light(-1000, 1000, -1000, 1000, -1000, 1000, level); }

    void set_loaded(int cx, int cz, bool loaded) {
        if (loaded) {
            unloaded_.erase({cx, cz});
        } else {
            unloaded_.insert({cx, cz});
        }
    }

    [[nodiscard]] std::uint16_t block_at(int wx, int wy, int wz) const override {
        std::uint16_t found = 0;
        for (const Region &region : blocks_) {
            if (contains(region, wx, wy, wz)) {
                found = region.value;
            }
        }
        return found;
    }

    [[nodiscard]] bool solid_at(int wx, int wy, int wz) const override { return block_at(wx, wy, wz) != 0; }

    [[nodiscard]] bool liquid_at(int wx, int wy, int wz) const override { return false; }

    [[nodiscard]] bool chunk_loaded(int cx, int cz) const override { return !unloaded_.contains({cx, cz}); }

    [[nodiscard]] int light_at(int wx, int wy, int wz) const override {
        int found = 15;
        for (const LightRegion &region : lights_) {
            if (contains(region, wx, wy, wz)) {
                found = region.level;
            }
        }
        return found;
    }

private:
    struct Region {
        int x0, x1, y0, y1, z0, z1;
        std::uint16_t value;
    };

    struct LightRegion {
        int x0, x1, y0, y1, z0, z1;
        int level;
    };

    template <typename R>
    [[nodiscard]] static bool contains(const R &region, int x, int y, int z) {
        return x >= region.x0 && x <= region.x1 && y >= region.y0 && y <= region.y1 && z >= region.z0 && z <= region.z1;
    }

    std::vector<Region> blocks_;
    std::vector<LightRegion> lights_;
    std::set<std::pair<int, int>> unloaded_;
};

// The registries, the rules, a world and a store - everything a mob rule test
// needs. The launch content is used on purpose (block ids from the shipping
// registry, mobs from MobRegistry::create_default), so the tests measure the
// mobs that ship.
struct MobFixture {
    gam::EntityTypeRegistry types = gam::EntityTypeRegistry::create_default();
    gam::ItemRegistry items = gam::ItemRegistry::create_default();
    // Registers the three launch mobs into `types` (declaration order matters:
    // the registry needs both halves built first).
    gam::MobRegistry mobs = gam::MobRegistry::create_default(types, items);
    srv::MobRules rules{};
    srv::MobSpawnRules spawn_rules{};
    MobTestWorld world;
    srv::EntityStore store;

    MobFixture() { world.floor(64, 1, 220); } // a stone floor, feet at y = 64

    // Block placement, forwarded so a test can build a wall between two mobs.
    MobFixture &fill(int x0, int x1, int y0, int y1, int z0, int z1, std::uint16_t block) {
        world.fill(x0, x1, y0, y1, z0, z1, block);
        return *this;
    }

    [[nodiscard]] std::uint16_t type_of(const std::string_view id) const { return types.id_of(id); }

    [[nodiscard]] srv::EntityId add_mob(const std::string_view id, const glm::dvec3 &position,
                                        const std::uint64_t seed = 0x51ED2701ULL) {
        return srv::spawn_mob(store, mobs, types.id_of(id), position, seed, rules);
    }

    [[nodiscard]] srv::Entity &get(const srv::EntityId id) { return *store.find(id); }

    [[nodiscard]] const srv::Entity &get(const srv::EntityId id) const { return *store.find(id); }

    // One mob step. `actor` is the pose the authority would have been given; an
    // empty optional means "nobody has told the authority where the player is",
    // which is what a headless world looks like.
    [[nodiscard]] srv::MobStepResult step(const std::optional<gam::ActorPose> &actor,
                                          const gam::Difficulty difficulty = gam::Difficulty::Normal,
                                          const int ticks = 1, const std::uint64_t seed = 0x51ED2701ULL) {
        srv::MobStepResult result;
        for (int i = 0; i < ticks; ++i) {
            result = srv::step_mobs(store, world, types, mobs, rules, difficulty, actor.has_value() ? &*actor : nullptr,
                                    seed);
        }
        return result;
    }

    // A standing player at (x, 64, z) looking straight ahead, optionally holding
    // `held`.
    [[nodiscard]] static gam::ActorPose actor_at(double x, double z, std::uint16_t held = 0) {
        gam::ActorPose pose;
        pose.feet = {x, 64.0, z};
        pose.height = opencraft::physics::PlayerState::kStandingHeight;
        pose.eye_height = 1.62;
        pose.held_item = held;
        return pose;
    }
};

} // namespace mobtest
