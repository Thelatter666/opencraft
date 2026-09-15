#include "tick.hpp"

#include <cmath>

// GLFW must not pull in a GL header of its own post-split (main.cpp does the
// same): glad is loaded separately.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "client_config.hpp"
#include "opencraft/core/log.hpp"
#include "opencraft/game/placement.hpp"
#include "opencraft/game/raycast.hpp"
#include "opencraft/physics/auto_jump.hpp"
#include "opencraft/physics/input_state.hpp"
#include "opencraft/physics/player_physics.hpp"
#include "world.hpp"

namespace phy = opencraft::physics;
namespace gam = opencraft::game;

namespace opencraft::client {

bool key_pressed(GLFWwindow *window, int key) {
    return glfwGetKey(window, key) == GLFW_PRESS;
}

glm::dvec3 view_dir(double view_yaw, double view_pitch) {
    const double cp = std::cos(view_pitch);
    return glm::dvec3(-std::sin(view_yaw) * cp, -std::sin(view_pitch), -std::cos(view_yaw) * cp);
}

// Level snapshot for periodic + exit saves (T009). Field-for-field the
// inverse of storage::serialize_level - keep both in sync.
storage::LevelData make_level_data(const TickContext &ctx) {
    storage::LevelData level;
    level.seed = ctx.world_seed;
    level.tick_count = ctx.game_ticks;
    level.has_player = true;
    level.spawn_x = ctx.spawn_pos.x;
    level.spawn_y = ctx.spawn_pos.y;
    level.spawn_z = ctx.spawn_pos.z;
    level.player_x = ctx.curr_state.position.x;
    level.player_y = ctx.curr_state.position.y;
    level.player_z = ctx.curr_state.position.z;
    level.player_vx = ctx.curr_state.velocity.x;
    level.player_vy = ctx.curr_state.velocity.y;
    level.player_vz = ctx.curr_state.velocity.z;
    level.yaw = ctx.view_yaw;
    level.pitch = ctx.view_pitch;
    level.health = ctx.curr_state.health;
    level.fall_peak_y = ctx.curr_state.fall_peak_y;
    level.fall_distance = ctx.curr_state.fall_distance;
    level.pose = static_cast<std::uint8_t>(ctx.curr_state.pose);
    level.on_ground = ctx.curr_state.on_ground;
    level.selected_block = ctx.state.selected_block;
    return level;
}

// One 20 TPS logic tick: physics -> targeting -> mining -> placement.
void run_tick(const TickContext &ctx) {
    // ── input mapping (WASD + space + shift + ctrl) ──────────────────────
    // T-D1: S maps to the explicit InputState::backward field (T007 had
    // no backward flag and simulated it as a 180° yaw flip; the physics
    // input direction now handles backward directly, and sprint requires
    // forward + not-backward per MC). forward_press is the one-tick W
    // keydown edge that drives the physics double-tap sprint window.
    const bool w = key_pressed(ctx.window, GLFW_KEY_W);
    const bool s = key_pressed(ctx.window, GLFW_KEY_S);
    const bool a = key_pressed(ctx.window, GLFW_KEY_A);
    const bool d = key_pressed(ctx.window, GLFW_KEY_D);
    phy::InputState in;
    in.yaw = ctx.view_yaw;
    in.pitch = ctx.view_pitch;
    in.forward = w && !s;
    in.backward = s && !w;
    in.left = a;
    in.right = d;
    in.forward_press = w && !ctx.state.prev_w;
    ctx.state.prev_w = w;
    in.jump = key_pressed(ctx.window, GLFW_KEY_SPACE) != 0;
    in.sneak = key_pressed(ctx.window, GLFW_KEY_LEFT_SHIFT) != 0;
    in.sprint = key_pressed(ctx.window, GLFW_KEY_LEFT_CONTROL) != 0;

    // ── T-D14 Auto-Jump (card §4): input-stage injection ────────────────
    // Decide BEFORE the physics step (docs/research/08 §2: the mechanism
    // lives in the input stage of the tick). When the pure predicate says
    // the forward move ends against a 0.6–1.25-block obstacle with
    // headroom, set in.jump so the EXISTING jump branch in step_player
    // runs — manual-jump semantics (incl. the sprint +0.2 boost) come
    // free, and the golden numbers stay shared. The player's own jump
    // input short-circuits the call (nothing to inject).
    if (!in.jump && ctx.auto_jump_enabled) {
        phy::AutoJumpConfig aj_cfg; // defaults = card §1: ON, 1.0 scan, 1.8 clearance
        if (phy::should_auto_jump(ctx.curr_state, in, ctx.world, phy::PhysicsConfig{}, aj_cfg)) {
            in.jump = true;
        }
    }

    ctx.prev_state = ctx.curr_state;
    phy::step_player(ctx.curr_state, in, ctx.world);

    // Sprint transitions come from the physics state machine (explicit
    // state per the T-D1 contract) — log them for QA evidence.
    if (ctx.curr_state.sprinting != ctx.prev_state.sprinting) {
        OC_LOG_INFO("sprint {} at tick {} (pos {:.2f}, {:.2f}, {:.2f})", ctx.curr_state.sprinting ? "start" : "stop",
                    ctx.game_ticks, ctx.curr_state.position.x, ctx.curr_state.position.y, ctx.curr_state.position.z);
    }

    // Sprint-jump arc measurement (acceptance 6c). A sprint jump takes off
    // from the ground while sprinting; the arc closes when the player is
    // grounded again, and the horizontal centre-to-centre distance and the
    // tick count give an on-machine average speed comparable to the
    // headless ⚖ figure (12-move arc, see test_sprint_feel.cpp).
    // NOTE: take-off is detected from prev_state.on_ground — step_player
    // applies the jump, so curr_state is already airborne on this tick.
    if (!ctx.state.jump_arc_open && ctx.curr_state.sprinting && ctx.prev_state.on_ground && in.jump) {
        ctx.state.jump_arc_open = true;
        ctx.state.jump_arc_start = ctx.curr_state.position;
        ctx.state.jump_arc_ticks = 0;
    } else if (ctx.state.jump_arc_open) {
        ++ctx.state.jump_arc_ticks;
        if (ctx.curr_state.on_ground) {
            const double dx = ctx.curr_state.position.x - ctx.state.jump_arc_start.x;
            const double dz = ctx.curr_state.position.z - ctx.state.jump_arc_start.z;
            const double dist = std::sqrt(dx * dx + dz * dz);
            // Move count includes the take-off tick itself, matching the
            // headless 12-move arc convention in test_sprint_feel.cpp.
            const int moves = ctx.state.jump_arc_ticks + 1;
            OC_LOG_INFO("sprint-jump arc: {} moves, horizontal {:.3f} blocks, clearance {:.3f}, "
                        "avg {:.3f} m/s (from ({:.2f}, {:.2f}, {:.2f}))",
                        moves, dist, dist - 0.6, dist * 20.0 / moves, ctx.state.jump_arc_start.x,
                        ctx.state.jump_arc_start.y, ctx.state.jump_arc_start.z);
            ctx.state.jump_arc_open = false;
        } else if (ctx.state.jump_arc_ticks > 60) {
            ctx.state.jump_arc_open = false; // safety: never let a stuck arc log forever
        }
    }

    // ── targeting ────────────────────────────────────────────────────────
    ctx.state.bucket_selected = ctx.state.selected_slot == kBucketSlot;
    // An empty bucket is aimed at water, so liquids become targetable for
    // it; everything else keeps the T008 filter (aim through water).
    const bool bucket_filling = ctx.state.bucket_selected && !ctx.state.bucket_has_water;
    const double eye_height = ctx.curr_state.pose == phy::Pose::Sneaking ? kEyeSneaking : kEyeStanding;
    const glm::dvec3 eye = ctx.curr_state.position + glm::dvec3(0.0, eye_height, 0.0);
    const auto filter = [&](std::uint16_t id) {
        const bool liquid = id != 0 && ctx.world.registry().def_of(id).liquid;
        if (bucket_filling) {
            return id != 0; // water surfaces are targetable
        }
        return id != 0 && !liquid; // liquids are not targetable
    };
    const gam::VoxelRayHit hit =
        gam::raycast_voxel(eye, view_dir(ctx.view_yaw, ctx.view_pitch), kReachDistance, ctx.world, filter);
    ctx.state.has_target = hit.hit;
    ctx.state.target_pos = hit.block_pos;

    // ── mining ───────────────────────────────────────────────────────────
    const bool left_held = glfwGetMouseButton(ctx.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const std::uint16_t target_id = hit.hit ? ctx.world.block_at(hit.block_pos.x, hit.block_pos.y, hit.block_pos.z) : 0;
    const auto mining_tick = ctx.mining.tick(hit.block_pos, target_id, hit.hit, left_held);
    if (left_held && hit.hit && !ctx.state.swinging) {
        ctx.state.swinging = true;
        ctx.state.swing_start = glfwGetTime();
    }
    if (mining_tick.broke) {
        ctx.world.set_block(hit.block_pos.x, hit.block_pos.y, hit.block_pos.z, 0, ctx.dirty_chunks);
        ctx.state.crack_stage = -1;
        // Break particles: block main color with brightness jitter (T009).
        const std::uint16_t broken_id = target_id;
        if (broken_id < ctx.block_colors.size()) {
            const glm::dvec3 center = glm::dvec3(hit.block_pos) + glm::dvec3(0.5, 0.5, 0.5);
            std::uint32_t rng = static_cast<std::uint32_t>(hit.block_pos.x * 73856093) ^
                                static_cast<std::uint32_t>(hit.block_pos.y * 19349663) ^
                                static_cast<std::uint32_t>(hit.block_pos.z * 83492791) ^
                                static_cast<std::uint32_t>(ctx.game_ticks * 2654435761u);
            const auto next_rand = [&rng]() {
                rng = rng * 1664525u + 1013904223u;
                return static_cast<float>(rng >> 8) / static_cast<float>(1 << 24);
            };
            for (int i = 0; i < kParticlesPerBreak && ctx.particles.size() < kMaxParticles; ++i) {
                Particle p;
                p.pos = center + glm::dvec3(next_rand() - 0.5, next_rand() - 0.5, next_rand() - 0.5) * 0.6;
                p.vel = glm::vec3(next_rand() - 0.5, next_rand(), next_rand() - 0.5) * 3.5f;
                p.life = 0.4f + next_rand() * 0.25f;
                const float jitter = 0.75f + next_rand() * 0.5f;
                p.color = glm::vec4(glm::clamp(ctx.block_colors[broken_id] * jitter, 0.0f, 1.0f), 1.0f);
                ctx.particles.push_back(p);
            }
        }
        ctx.state.swinging = true;
        ctx.state.swing_start = glfwGetTime();
    } else if (hit.hit && mining_tick.progress > 0.0f) {
        ctx.state.crack_pos = hit.block_pos;
        ctx.state.crack_stage = mining_tick.crack_stage;
    } else {
        ctx.state.crack_stage = -1;
    }

    // ── placement (hold right: first attempt immediate, then every 4 ticks
    //    ⚖ docs/01 §4; interactive blocks would take priority here - none
    //    exist in this milestone, the hook stays at the call site) ────────
    const bool right_held = glfwGetMouseButton(ctx.window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (ctx.state.place_cooldown > 0) {
        --ctx.state.place_cooldown;
    }
    // The block path repeats every 4 ticks while the button is held
    // (docs/01 §4). The bucket must NOT: a placed source is immediately
    // pickable again, so a held button alternates pour -> scoop (observed
    // on-machine: pour then scoop 4 ticks later), which reads as a broken
    // item. Item use is therefore edge-triggered.
    const bool use_edge = !ctx.state.prev_right;
    if (right_held && (ctx.state.place_cooldown == 0 || !ctx.state.prev_right)) {
        if (hit.hit && (!ctx.state.bucket_selected || use_edge)) {
            if (ctx.state.bucket_selected) {
                // ── bucket (T-F1) ────────────────────────────────────────
                // Filled: pour a source into the cell the hit face opens
                // onto. Empty: scoop a source block out of the world.
                // Placement skips check_placement's player-overlap test on
                // purpose: fluids are non-solid, so pouring water at your
                // own feet is legal (MC does the same); the replaceable
                // test is the same one the block path uses.
                if (ctx.state.bucket_has_water) {
                    const glm::ivec3 cell = gam::placement_cell(hit);
                    const std::uint16_t occupant = ctx.world.block_at(cell.x, cell.y, cell.z);
                    if (gam::is_replaceable(ctx.world.registry(), occupant) &&
                        ctx.world.place_water_source(cell.x, cell.y, cell.z)) {
                        ctx.state.bucket_has_water = false;
                        OC_LOG_INFO("bucket: poured water source at ({}, {}, {})", cell.x, cell.y, cell.z);
                    }
                } else if (ctx.world.remove_water_source(hit.block_pos.x, hit.block_pos.y, hit.block_pos.z,
                                                         ctx.dirty_chunks)) {
                    ctx.state.bucket_has_water = true;
                    OC_LOG_INFO("bucket: filled from ({}, {}, {})", hit.block_pos.x, hit.block_pos.y, hit.block_pos.z);
                }
            } else {
                const glm::ivec3 cell = gam::placement_cell(hit);
                const auto status = gam::check_placement(ctx.world.registry(), ctx.world, cell, ctx.curr_state.position,
                                                         ctx.curr_state.height(), phy::PlayerState::kHalfWidth);
                if (status == gam::PlacementStatus::Ok) {
                    ctx.world.set_block(cell.x, cell.y, cell.z, ctx.state.selected_block, ctx.dirty_chunks);
                    OC_LOG_INFO("placed {} at ({}, {}, {})", ctx.world.registry().string_of(ctx.state.selected_block),
                                cell.x, cell.y, cell.z);
                }
            }
        }
        ctx.state.place_cooldown = 4; // ⚖ retry rhythm whether or not the attempt succeeded
        ctx.state.swinging = true;
        ctx.state.swing_start = glfwGetTime();
    }
    ctx.state.prev_right = right_held;

    // ── hotbar selection (blocks on 1..9, the bucket on 0) ───────────────
    for (int slot = 0; slot < 9; ++slot) {
        if (key_pressed(ctx.window, GLFW_KEY_1 + slot)) {
            ctx.state.selected_slot = slot;
            ctx.state.selected_block = ctx.state.hotbar[slot];
        }
    }
    if (key_pressed(ctx.window, GLFW_KEY_0)) {
        ctx.state.selected_slot = kBucketSlot;
        ctx.state.selected_block = ctx.world.water_block_id();
    }

    // ── fluid scheduled ticks (T-F1): one step per game tick, exactly like
    //    the rest of the simulation. Changed chunks go to the remesh list.
    ctx.world.fluid_step(ctx.dirty_chunks);

    // ── autosave cadence: 200 ticks = ~10 s of game time (T009) ──────────
    if (ctx.save.maybe_autosave_tick()) {
        const std::size_t chunks = ctx.world.autosave_pass();
        ctx.save.write_level_now(make_level_data(ctx));
        OC_LOG_INFO("autosave: {} chunk(s) queued for async write, level written (ticks={})", chunks, ctx.game_ticks);
    }
}

} // namespace opencraft::client
