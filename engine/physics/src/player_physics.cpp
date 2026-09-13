#include "opencraft/physics/player_physics.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace opencraft::physics {
namespace {

// Working AABB in double precision (opencraft::core::AABB is float-based and
// the sim requires bit-stable doubles for the golden replay).
struct Box {
    double min_x;
    double min_y;
    double min_z;
    double max_x;
    double max_y;
    double max_z;
};

[[nodiscard]] Box box_of(const PlayerState &s) {
    const double height = s.height();
    return Box{s.position.x - PlayerState::kHalfWidth, s.position.y,          s.position.z - PlayerState::kHalfWidth,
               s.position.x + PlayerState::kHalfWidth, s.position.y + height, s.position.z + PlayerState::kHalfWidth};
}

// Strict voxel overlap: a box face exactly touching a block face is NOT a
// collision, which keeps the "clamped against wall" state quiescent.
[[nodiscard]] bool box_collides(const IBlockSource &world, const Box &b) {
    const int x0 = static_cast<int>(std::floor(b.min_x));
    const int x1 = static_cast<int>(std::floor(b.max_x));
    const int y0 = static_cast<int>(std::floor(b.min_y));
    const int y1 = static_cast<int>(std::floor(b.max_y));
    const int z0 = static_cast<int>(std::floor(b.min_z));
    const int z1 = static_cast<int>(std::floor(b.max_z));
    for (int bx = x0; bx <= x1; ++bx) {
        for (int by = y0; by <= y1; ++by) {
            for (int bz = z0; bz <= z1; ++bz) {
                if (b.max_x > bx && b.min_x < bx + 1 && b.max_y > by && b.min_y < by + 1 && b.max_z > bz &&
                    b.min_z < bz + 1 && world.solid_at(bx, by, bz)) {
                    return true;
                }
            }
        }
    }
    return false;
}

// True when some solid block overlaps the box footprint one probe-layer below
// the feet (i.e. the player would still stand after shifting horizontally).
[[nodiscard]] bool has_support_at(const PlayerState &s, const IBlockSource &world, double offset_x, double offset_z,
                                  double probe_depth) {
    const int layer = static_cast<int>(std::floor(s.position.y - probe_depth));
    const double min_x = s.position.x - PlayerState::kHalfWidth + offset_x;
    const double max_x = s.position.x + PlayerState::kHalfWidth + offset_x;
    const double min_z = s.position.z - PlayerState::kHalfWidth + offset_z;
    const double max_z = s.position.z + PlayerState::kHalfWidth + offset_z;
    const int x0 = static_cast<int>(std::floor(min_x));
    const int x1 = static_cast<int>(std::floor(max_x));
    const int z0 = static_cast<int>(std::floor(min_z));
    const int z1 = static_cast<int>(std::floor(max_z));
    for (int bx = x0; bx <= x1; ++bx) {
        for (int bz = z0; bz <= z1; ++bz) {
            if (max_x > bx && min_x < bx + 1 && max_z > bz && min_z < bz + 1 && world.solid_at(bx, layer, bz)) {
                return true;
            }
        }
    }
    return false;
}

// Sneak edge protection (docs/01 §2 手感项): shrink the horizontal
// displacement in ≤0.05-block steps (x first, then z) until the shifted box
// keeps ground support. Simplified MC backoff — the velocity itself is left
// untouched; only this tick's offset is reduced.
void back_off_from_edge(const PlayerState &s, const IBlockSource &world, const PhysicsConfig &cfg, double &dx,
                        double &dz) {
    const double step = cfg.sneak_edge_step;
    while ((dx != 0.0 || dz != 0.0) && !has_support_at(s, world, dx, dz, cfg.ground_probe_depth)) {
        if (dx != 0.0) {
            dx = std::abs(dx) <= step ? 0.0 : dx - (dx > 0.0 ? step : -step);
        } else {
            dz = std::abs(dz) <= step ? 0.0 : dz - (dz > 0.0 ? step : -step);
        }
    }
}

// Feet block and a mid-body probe decide "in water" (good-enough heuristic
// per the T007 card; exact swim feel is deferred).
[[nodiscard]] bool is_in_water(const IBlockSource &world, const PlayerState &s) {
    const int x = static_cast<int>(std::floor(s.position.x));
    const int z = static_cast<int>(std::floor(s.position.z));
    const int feet = static_cast<int>(std::floor(s.position.y + 0.01));
    const int mid = static_cast<int>(std::floor(s.position.y + 1.0));
    return world.liquid_at(x, feet, z) || world.liquid_at(x, mid, z);
}

// Yaw → normalized horizontal input direction. forward = (-sin, -cos);
// right = forward × up. Components under 1e-12 are snapped to zero so
// axis-aligned yaws (the golden replay only uses those) are bit-stable
// across libm implementations.
[[nodiscard]] std::pair<double, double> input_direction(const InputState &in) {
    double fx = -std::sin(in.yaw);
    double fz = -std::cos(in.yaw);
    if (std::abs(fx) < 1e-12) {
        fx = 0.0;
    }
    if (std::abs(fz) < 1e-12) {
        fz = 0.0;
    }
    const double rx = -fz;
    const double rz = fx;
    double dx = fx * (in.forward ? 1.0 : 0.0) + rx * ((in.right ? 1.0 : 0.0) - (in.left ? 1.0 : 0.0));
    double dz = fz * (in.forward ? 1.0 : 0.0) + rz * ((in.right ? 1.0 : 0.0) - (in.left ? 1.0 : 0.0));
    const double len_sq = dx * dx + dz * dz;
    if (len_sq > 0.0) {
        const double inv = 1.0 / std::sqrt(len_sq);
        dx *= inv;
        dz *= inv;
    }
    return {dx, dz};
}

// Per-axis clamped moves (docs/research/03 §6.1: Y → X → Z). Each returns
// the actually-moved delta; substep penetration is < 1 block, so clamping
// against the single leading-edge block column/layer is sufficient.
double move_axis_y(PlayerState &s, const IBlockSource &world, double dy, bool &hit_ground, bool &hit_ceiling) {
    if (dy == 0.0) {
        return 0.0;
    }
    const double before = s.position.y;
    s.position.y += dy;
    if (!box_collides(world, box_of(s))) {
        return s.position.y - before;
    }
    if (dy < 0.0) {
        const int by = static_cast<int>(std::floor(s.position.y));
        s.position.y = static_cast<double>(by) + 1.0;
        hit_ground = true;
    } else {
        const int by = static_cast<int>(std::floor(s.position.y + s.height()));
        s.position.y = static_cast<double>(by) - s.height();
        hit_ceiling = true;
    }
    return s.position.y - before;
}

double move_axis_x(PlayerState &s, const IBlockSource &world, double dx) {
    if (dx == 0.0) {
        return 0.0;
    }
    const double before = s.position.x;
    s.position.x += dx;
    if (!box_collides(world, box_of(s))) {
        return s.position.x - before;
    }
    if (dx > 0.0) {
        const int bx = static_cast<int>(std::floor(s.position.x + PlayerState::kHalfWidth));
        s.position.x = static_cast<double>(bx) - PlayerState::kHalfWidth;
    } else {
        const int bx = static_cast<int>(std::floor(s.position.x - PlayerState::kHalfWidth));
        s.position.x = static_cast<double>(bx) + 1.0 + PlayerState::kHalfWidth;
    }
    s.velocity.x = 0.0;
    return s.position.x - before;
}

double move_axis_z(PlayerState &s, const IBlockSource &world, double dz) {
    if (dz == 0.0) {
        return 0.0;
    }
    const double before = s.position.z;
    s.position.z += dz;
    if (!box_collides(world, box_of(s))) {
        return s.position.z - before;
    }
    if (dz > 0.0) {
        const int bz = static_cast<int>(std::floor(s.position.z + PlayerState::kHalfWidth));
        s.position.z = static_cast<double>(bz) - PlayerState::kHalfWidth;
    } else {
        const int bz = static_cast<int>(std::floor(s.position.z - PlayerState::kHalfWidth));
        s.position.z = static_cast<double>(bz) + 1.0 + PlayerState::kHalfWidth;
    }
    s.velocity.z = 0.0;
    return s.position.z - before;
}

} // namespace

void step_player(PlayerState &s, const InputState &in, const IBlockSource &world, const PhysicsConfig &cfg) {
    s.last_input_sequence = in.sequence;

    // Pose: crouch immediately; standing up requires headroom for the taller
    // box (MC behavior — stay sneaking when the ceiling blocks it).
    if (in.sneak) {
        s.pose = Pose::Sneaking;
    } else if (s.pose == Pose::Sneaking) {
        Box standing = box_of(s);
        standing.max_y = s.position.y + PlayerState::kStandingHeight;
        if (!box_collides(world, standing)) {
            s.pose = Pose::Standing;
        }
    }

    // Ground support re-verification: walking off an edge must be detected
    // before the move (the Y move sees dy == 0 while standing).
    if (s.on_ground && !has_support_at(s, world, 0.0, 0.0, cfg.ground_probe_depth)) {
        s.on_ground = false;
    }

    const bool water = is_in_water(world, s);

    // Horizontal integration: v = v·drag + dir·accel with accel chosen so
    // the steady state is exactly the mode's target speed. Airborne control
    // is weaker (research/01 §1.2); water additionally scales the target.
    const auto [dir_x, dir_z] = input_direction(in);
    const bool sprinting = in.sprint && in.forward && !in.sneak;
    double target = in.sneak ? cfg.sneak_speed : (sprinting ? cfg.sprint_speed : cfg.walk_speed);
    if (water) {
        target *= cfg.water_speed_mult;
    }
    const double drag = water ? cfg.water_drag : (s.on_ground ? cfg.ground_drag : cfg.air_drag);
    // Airborne acceleration is anchored to WALK speed, not the mode speed:
    // the air steady state is walk_speed, so jumps carry their takeoff speed
    // (a sprint decays toward walk mid-air) without being able to gain.
    const double accel_source = (!s.on_ground && !water) ? cfg.walk_speed * cfg.air_control : target;
    const double accel = accel_source * (1.0 - drag);
    s.velocity.x = s.velocity.x * drag + dir_x * accel;
    s.velocity.z = s.velocity.z * drag + dir_z * accel;

    double dx = s.velocity.x;
    double dz = s.velocity.z;
    if (s.on_ground && in.sneak) {
        back_off_from_edge(s, world, cfg, dx, dz);
    }

    // Ground jump sets the velocity BEFORE the move (MC order: the first
    // tick's displacement is the full 0.42, which is what makes the apex
    // come out at exactly 1.2522 with the gravity/drag pair).
    if (in.jump && s.on_ground && !water) {
        s.velocity.y = cfg.jump_velocity;
        s.on_ground = false;
    }

    // Substeps of at most max_substep blocks prevent tunneling
    // (docs/research/03 §6.2); axis order Y → X → Z per substep.
    const double max_component = std::max({std::abs(dx), std::abs(s.velocity.y), std::abs(dz)});
    int substeps = static_cast<int>(std::ceil(max_component / cfg.max_substep));
    if (substeps < 1) {
        substeps = 1;
    }

    for (int i = 0; i < substeps; ++i) {
        bool hit_ground = false;
        bool hit_ceiling = false;
        move_axis_y(s, world, s.velocity.y / substeps, hit_ground, hit_ceiling);
        if (hit_ground) {
            s.on_ground = true;
            s.velocity.y = 0.0;
            // Fall damage (docs/01 §2 ⚖): floor(fall_distance − 3) HP,
            // measured from the apex to the landing surface; water contact
            // at tick start means the landing is a water landing.
            const double fallen = s.fall_peak_y - s.position.y;
            if (!water && fallen > cfg.fall_damage_offset) {
                s.health -= std::floor(fallen - cfg.fall_damage_offset);
            }
        }
        if (hit_ceiling) {
            s.velocity.y = 0.0;
        }
        move_axis_x(s, world, dx / substeps);
        move_axis_z(s, world, dz / substeps);
    }

    // Fall-distance bookkeeping, measured against the arc apex so whole-block
    // drops land on exact values (no per-tick accumulation drift).
    if (water || s.on_ground) {
        s.fall_distance = 0.0;
        s.fall_peak_y = s.position.y;
    } else {
        if (s.velocity.y > 0.0) {
            s.fall_peak_y = s.position.y;
            s.fall_distance = 0.0;
        } else {
            if (s.fall_peak_y < s.position.y) {
                s.fall_peak_y = s.position.y;
            }
            s.fall_distance = s.fall_peak_y - s.position.y;
        }
    }

    // Vertical integration happens AFTER the move (this ordering is what the
    // ⚖ 1.2522 apex and the 78.4 m/s terminal velocity are derived from).
    if (water) {
        s.velocity.y = s.velocity.y * cfg.water_drag + (in.jump ? cfg.swim_up_accel : -cfg.water_gravity);
        if (s.velocity.y > cfg.water_max_up_speed) {
            s.velocity.y = cfg.water_max_up_speed;
        }
    } else if (!s.on_ground) {
        s.velocity.y = (s.velocity.y - cfg.gravity) * cfg.vertical_drag;
    } else {
        s.velocity.y = 0.0;
    }
}

} // namespace opencraft::physics
