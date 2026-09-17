// T-D40 zero-change A/B harness (evidence device, NOT product code).
//
// The same source is compiled TWICE - once against the pre-refactor tree and
// once against the refactored tree - and both binaries must print the exact
// same bytes. It only ever touches the PUBLIC entry points that the card does
// not change (physics::step_player, server::step_items / spawn_item_drop), so
// this file needs no per-tree #ifdef: any difference in the output is a
// difference in the collision implementation.
//
// Doubles are printed as their raw bit pattern (std::bit_cast), which is
// strictly stronger than %.17g: it also catches 0.0 vs -0.0 and any ULP drift.
//
// Coverage: every branch of the two collision paths the refactor touched:
//   player - X clamp, Z clamp, Y landing (height-aware 0.25 face), ceiling
//            clamp, step-assist, sneak edge back-off, ice friction, water
//            swim, long fall + fall damage, pose under a ceiling;
//   drops  - landing on a full block, landing on a 0.25 slab (height-aware),
//            corner clamp in X and Z, ice slide, merge of a resting pair,
//            long fall, void despawn.

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

#include "opencraft/game/entity_type.hpp"
#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/item_stack.hpp"
#include "opencraft/physics/block_source.hpp"
#include "opencraft/physics/input_state.hpp"
#include "opencraft/physics/physics_config.hpp"
#include "opencraft/physics/player_physics.hpp"
#include "opencraft/physics/player_state.hpp"
#include "opencraft/sim/entity_store.hpp"
#include "opencraft/sim/item_sim.hpp"
#include "opencraft/voxel/block_registry.hpp"

namespace gam = opencraft::game;
namespace srv = opencraft::server;
namespace voxel = opencraft::voxel;
namespace phy = opencraft::physics;

namespace {

std::uint64_t bits(const double value) {
    return std::bit_cast<std::uint64_t>(value);
}

void print_vec(const char *tag, const glm::dvec3 &v) {
    std::printf("%s[%016llx %016llx %016llx]", tag, (unsigned long long) bits(v.x), (unsigned long long) bits(v.y),
                (unsigned long long) bits(v.z));
}

// A block world with the shapes the collision code branches on: full cubes, a
// partial top face (height-aware landing / step-up), water and ice.
//
//   y = 63   ground plate, top face 64.0        x,z in [-20, 20]
//   x = 4    wall, y 64..66                     z in [-3, 3]
//   y = 67   ceiling plate                      x in [2, 3], z in [-3, 3]
//   (1,64,-6) top 0.5   (2,64,-6) top 0.5   (3,64,-6) top 0.25
//   water    x,z in [-14,-10], y in [64, 66]
//   ice      y = 63                             x in [-8,-6], z in [4, 6]
class AbWorld final : public srv::IItemWorld {
public:
    struct Region {
        int x0, x1, y0, y1, z0, z1;
    };

    AbWorld &fill(int x0, int x1, int y0, int y1, int z0, int z1) {
        solids_.push_back({x0, x1, y0, y1, z0, z1});
        return *this;
    }

    AbWorld &slab(int x, int y, int z, double top) {
        slabs_.push_back({x, y, z, top});
        return *this;
    }

    AbWorld &water(int x0, int x1, int y0, int y1, int z0, int z1) {
        liquids_.push_back({x0, x1, y0, y1, z0, z1});
        return *this;
    }

    AbWorld &ice(int x0, int x1, int y0, int y1, int z0, int z1) {
        ice_.push_back({x0, x1, y0, y1, z0, z1});
        return *this;
    }

    [[nodiscard]] bool solid_at(int wx, int wy, int wz) const override {
        for (const Region &r : solids_) {
            if (in(r, wx, wy, wz)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool liquid_at(int wx, int wy, int wz) const override {
        for (const Region &r : liquids_) {
            if (in(r, wx, wy, wz)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] double slipperiness_at(int wx, int wy, int wz) const override {
        for (const Region &r : ice_) {
            if (in(r, wx, wy, wz)) {
                return 0.98;
            }
        }
        return phy::kDefaultSlipperiness;
    }

    [[nodiscard]] double shape_top_at(int wx, int wy, int wz) const override {
        for (const Slab &s : slabs_) {
            if (s.x == wx && s.y == wy && s.z == wz) {
                return s.top;
            }
        }
        return solid_at(wx, wy, wz) ? 1.0 : 0.0;
    }

    [[nodiscard]] std::uint16_t block_at(int wx, int wy, int wz) const override {
        if (liquid_at(wx, wy, wz)) {
            return 2;
        }
        return solid_at(wx, wy, wz) ? 1 : 0;
    }

    [[nodiscard]] bool chunk_loaded(int, int) const override { return true; }

private:
    struct Slab {
        int x, y, z;
        double top;
    };

    static bool in(const Region &r, int x, int y, int z) {
        return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1 && z >= r.z0 && z <= r.z1;
    }

    std::vector<Region> solids_;
    std::vector<Region> liquids_;
    std::vector<Region> ice_;
    std::vector<Slab> slabs_;
};

AbWorld make_world() {
    AbWorld world;
    world.fill(-20, 20, 60, 63, -20, 20); // ground plate, top face at y = 64
    world.fill(4, 4, 64, 66, -3, 3);      // wall
    world.fill(2, 3, 67, 67, -3, 3);      // ceiling
    world.slab(1, 64, -6, 0.5);           // step-assist target
    world.slab(2, 64, -6, 0.5);           // step-assist target
    world.slab(3, 64, -6, 0.25);          // height-aware landing face
    world.water(-14, -10, 64, 66, -14, -10);
    world.ice(-8, -6, 63, 63, 4, 6);
    return world;
}

// ── player cases ────────────────────────────────────────────────────────────
struct PlayerCase {
    const char *name;
    glm::dvec3 position;
    double yaw;
    int ticks;
    phy::InputState in;
};

// One case: place the player, run the scripted input, print every tick.
void run_player_case(const AbWorld &world, const phy::PhysicsConfig &cfg, const PlayerCase &c) {
    phy::PlayerState s;
    s.position = c.position;
    s.yaw = c.yaw;
    s.on_ground = true;
    s.fall_peak_y = c.position.y;
    std::printf("-- player case %s --\n", c.name);
    for (int tick = 0; tick < c.ticks; ++tick) {
        phy::InputState in = c.in;
        in.yaw = c.yaw;
        in.sequence = static_cast<std::uint32_t>(tick);
        phy::MoveResult result;
        phy::step_player(s, in, world, cfg, &result);
        std::printf("t%03d ", tick);
        print_vec("p=", s.position);
        print_vec(" v=", s.velocity);
        std::printf(" g=%d pose=%d hp=%016llx fd=%016llx fpy=%016llx coll=%d r=%d%d%d%d%d\n", s.on_ground ? 1 : 0,
                    static_cast<int>(s.pose), (unsigned long long) bits(s.health),
                    (unsigned long long) bits(s.fall_distance), (unsigned long long) bits(s.fall_peak_y),
                    s.collided_horizontally ? 1 : 0, result.hit_x ? 1 : 0, result.hit_y ? 1 : 0, result.hit_z ? 1 : 0,
                    result.landed ? 1 : 0, result.stepped ? 1 : 0);
    }
    std::printf("   end g=%d pose=%d hp=%016llx\n", s.on_ground ? 1 : 0, static_cast<int>(s.pose),
                (unsigned long long) bits(s.health));
}

void run_player_cases(const AbWorld &world, const phy::PhysicsConfig &cfg) {
    phy::InputState walk;
    walk.forward = true;
    phy::InputState walk_sprint = walk;
    walk_sprint.sprint = true;
    phy::InputState walk_sneak = walk;
    walk_sneak.sneak = true;
    phy::InputState jump = walk;
    jump.jump = true;
    phy::InputState stand;

    // +X into the wall at x = 4: the X clamp, then a jump right under it.
    run_player_case(world, cfg, {"X wall clamp + jump", {0.5, 64.0, 0.5}, -std::numbers::pi / 2.0, 130, walk});
    // Jump under the ceiling plate at x in [2,3]: the ceiling clamp.
    run_player_case(world, cfg, {"ceiling clamp", {2.5, 64.0, 0.5}, -std::numbers::pi / 2.0, 60, jump});
    // Walk -Z into the wall's +Z face from z = 5: the Z clamp.
    run_player_case(world, cfg, {"Z wall clamp", {4.3, 64.0, 5.0}, 0.0, 60, walk});
    // Step up onto the 0.5 slabs: step-assist must fire and keep speed.
    run_player_case(world, cfg, {"step assist 0.5", {1.5, 64.0, -3.0}, 0.0, 90, walk});
    // Fall onto the 0.25 slab face: height-aware landing (T-D8).
    run_player_case(world, cfg, {"land on 0.25 slab", {3.5, 68.0, -5.5}, 0.0, 80, stand});
    // Sneak along the plate edge: the edge back-off must hold the player up.
    run_player_case(world, cfg, {"sneak edge", {0.5, 64.0, -18.6}, 0.0, 80, walk_sneak});
    // Sprint on ice: the friction product (block half = 0.98).
    run_player_case(world, cfg, {"ice sprint", {-7.5, 64.0, 5.5}, std::numbers::pi, 120, walk_sprint});
    // Swim in the pool, then jump out of the water.
    run_player_case(world, cfg, {"water swim", {-12.5, 64.0, -12.5}, std::numbers::pi / 2.0, 100, jump});
    // Walk off the plate into the void: long fall, terminal velocity, damage.
    run_player_case(world, cfg, {"void fall", {25.5, 64.0, 0.5}, 0.0, 200, walk});
    // Sneak under the ceiling and try to stand up (pose stays Sneaking).
    run_player_case(world, cfg, {"pose under ceiling", {2.5, 64.0, 0.5}, 0.0, 40, walk_sneak});
}

// ── drop cases ──────────────────────────────────────────────────────────────
struct DropSpawn {
    glm::dvec3 position;
    glm::dvec3 velocity;
    int count;
};

void run_drop_case(const AbWorld &world, const char *name, const std::vector<DropSpawn> &spawns, const int ticks,
                   const int every) {
    gam::EntityTypeRegistry types = gam::EntityTypeRegistry::create_default();
    gam::ItemRegistry items = gam::ItemRegistry::create_default();
    voxel::BlockRegistry blocks = voxel::BlockRegistry::create_default();
    srv::ItemRules rules;
    srv::EntityStore store;
    const srv::ItemBlockHazard hazard(blocks);
    const std::uint16_t item = items.id_of("sod_loam");
    const std::uint16_t type = types.id_of("item");

    for (const DropSpawn &spawn : spawns) {
        srv::Entity e;
        e.type = type;
        e.position = spawn.position;
        e.velocity = spawn.velocity;
        e.stack = gam::ItemStack::of(item, spawn.count);
        e.health = types.def_of(type).max_health;
        e.pickup_delay = rules.pickup_delay_natural;
        e.merge_timer = rules.merge_period;
        e.last_block = glm::ivec3(glm::floor(e.position));
        static_cast<void>(store.spawn(e));
    }

    std::printf("-- drop case %s --\n", name);
    for (int tick = 0; tick < ticks; ++tick) {
        srv::step_items(store, world, hazard, rules, types, items);
        if (tick % every != 0 && tick >= 40) {
            continue;
        }
        std::printf("t%03d n=%zu", tick, store.live_ids().size());
        for (const srv::EntityId id : store.live_ids()) {
            const srv::Entity *e = store.find(id);
            if (e == nullptr) {
                continue;
            }
            std::printf(" #%u", id);
            print_vec("p=", e->position);
            print_vec("v=", e->velocity);
            std::printf(" g=%d age=%d mt=%d pd=%d n=%d", e->on_ground ? 1 : 0, e->age, e->merge_timer,
                        e->pickup_delay, e->stack.count);
        }
        std::printf("\n");
    }
    std::printf("   end n=%zu\n", store.live_ids().size());
}

void run_drop_cases(const AbWorld &world) {
    // Free fall onto the plate, then at rest (terminal + momentum cut-off).
    run_drop_case(world, "drop onto plate", {{{0.5, 70.0, 0.5}, {0.0, 0.0, 0.0}, 1}}, 60, 10);
    // Fall onto the 0.25 slab: the height-aware landing face at y = 64.25.
    run_drop_case(world, "drop onto 0.25 slab", {{{3.5, 68.0, -5.5}, {0.0, 0.0, 0.0}, 1}}, 60, 10);
    // Thrown into the wall/ground corner: the X clamp and the Z clamp.
    run_drop_case(world, "drop into corner", {{{2.0, 64.5, -1.0}, {0.6, 0.0, -0.6}, 1}}, 60, 10);
    // Ice: the friction product keeps it sliding for a long time.
    run_drop_case(world, "drop on ice", {{{-7.5, 64.5, 5.5}, {0.3, 0.0, 0.0}, 1}}, 120, 20);
    // A resting pair inside the merge box: the merge path (entity_box arithmetic).
    run_drop_case(world, "merge pair", {{{0.5, 64.5, 0.5}, {0.0, 0.0, 0.0}, 1}, {{0.6, 64.5, 0.5}, {0.0, 0.0, 0.0}, 2}},
                  120, 20);
    // Into the void: the long fall and the void despawn.
    run_drop_case(world, "void fall", {{{25.5, 70.0, 0.5}, {0.0, 0.0, 0.0}, 1}}, 200, 40);
}

} // namespace

int main() {
    const AbWorld world = make_world();
    const phy::PhysicsConfig cfg; // shipping defaults, untouched by the card
    std::printf("== player cases ==\n");
    run_player_cases(world, cfg);
    std::printf("== drop cases ==\n");
    run_drop_cases(world);
    std::printf("== done ==\n");
    return 0;
}
