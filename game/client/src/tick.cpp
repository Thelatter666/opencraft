#include "tick.hpp"

#include <algorithm>
#include <cmath>

// GLFW must not pull in a GL header of its own post-split (main.cpp does the
// same): glad is loaded separately.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "client_config.hpp"
#include "opencraft/core/log.hpp"
#include "opencraft/game/entity_pick.hpp"
#include "opencraft/game/pickup.hpp"
#include "opencraft/game/raycast.hpp"
#include "opencraft/physics/auto_jump.hpp"
#include "opencraft/physics/input_state.hpp"
#include "opencraft/physics/player_physics.hpp"
#include "opencraft/sim/item_sim.hpp"
#include "world.hpp"

namespace phy = opencraft::physics;
namespace gam = opencraft::game;
namespace srv = opencraft::server;

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
    // The saved field is the selected item's block form. An item without one
    // (a vessel, food, a tool) has no block to record and saves as air; the
    // selection is restored to the cell holding that block, or to the first
    // cell when there is none. Persisting the selection itself is M2c, with
    // the inventory.
    level.selected_block = ctx.state.selected_block == gam::kNoBlock ? 0 : ctx.state.selected_block;
    return level;
}

namespace {

// The actor geometry the authority validates an action against (T-A1): the
// position the physics step just produced, the current pose height and the eye
// offset for that pose. Value data, exactly what a client would send over the
// wire - the authority derives the eye and re-checks the reach itself.
[[nodiscard]] gam::ActorPose actor_pose(const TickContext &ctx) {
    const double eye_height = ctx.curr_state.pose == phy::Pose::Sneaking ? kEyeSneaking : kEyeStanding;
    gam::ActorPose pose{ctx.curr_state.position, ctx.curr_state.height(), eye_height};
    // T-M2: the mobs need to know what the player is holding, because the tempt
    // goal exists exactly while the breeding food is held and nothing is clicked.
    pose.held_item = ctx.state.selected_stack.item;
    return pose;
}

} // namespace

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
    // An empty vessel is aimed at water, so liquids become targetable for it
    // (T-F1 behaviour, now read off the held item instead of a bucket bool);
    // everything else keeps the T008 filter (aim through water).
    const bool vessel_filling = ctx.state.selected_use == ItemUse::FillVessel;
    const double eye_height = ctx.curr_state.pose == phy::Pose::Sneaking ? kEyeSneaking : kEyeStanding;
    const glm::dvec3 eye = ctx.curr_state.position + glm::dvec3(0.0, eye_height, 0.0);
    const auto filter = [&](std::uint16_t id) {
        const bool liquid = id != 0 && ctx.world.registry().def_of(id).liquid;
        if (vessel_filling) {
            return id != 0; // water surfaces are targetable
        }
        return id != 0 && !liquid; // liquids are not targetable
    };
    const glm::dvec3 look = view_dir(ctx.view_yaw, ctx.view_pitch);
    const gam::VoxelRayHit hit = gam::raycast_voxel(eye, look, kReachDistance, ctx.world, filter);
    ctx.state.has_target = hit.hit;
    ctx.state.target_pos = hit.block_pos;

    // ── entity targeting (T-M2) ──────────────────────────────────────────
    // The nearest mob whose box the view ray enters within ⚖ 3 blocks
    // (research/11 §1.5.1's player melee reach, game::kAttackReach). Iterating
    // the store's live ids rather than the entities themselves, because the ids
    // are a snapshot: nothing below mutates the store before the pick is used.
    {
        ctx.state.picked_mob = srv::EntityStore::kNoEntity;
        ctx.state.picked_mob_distance = 0.0;
        const srv::EntityStore &entities = ctx.world.entities();
        const gam::EntityTypeRegistry &types = ctx.world.entity_types();
        const gam::MobRegistry &mobs = ctx.world.mobs();
        for (const srv::EntityId id : entities.live_ids()) {
            const srv::Entity *entity = entities.find(id);
            if (entity == nullptr || mobs.find(entity->type) == nullptr) {
                continue;
            }
            const gam::EntityDef &def = types.def_of(entity->type);
            const auto [box_min, box_max] = srv::entity_box_corners(*entity, def);
            const std::optional<double> distance = gam::ray_box_entry(eye, look, box_min, box_max);
            if (!distance.has_value() || *distance > gam::kAttackReach) {
                continue;
            }
            if (ctx.state.picked_mob == srv::EntityStore::kNoEntity || *distance < ctx.state.picked_mob_distance) {
                ctx.state.picked_mob = id;
                ctx.state.picked_mob_distance = *distance;
            }
        }
    }
    // Is the mob in front of the BLOCK the ray hit? If not, the block wins (the
    // same nearest-hit rule the base game uses for a click).
    const bool mob_in_front =
        ctx.state.picked_mob != srv::EntityStore::kNoEntity && (!hit.hit || ctx.state.picked_mob_distance < hit.t);

    // ── mining ───────────────────────────────────────────────────────────
    const bool left_held = glfwGetMouseButton(ctx.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const std::uint16_t target_id = hit.hit ? ctx.world.block_at(hit.block_pos.x, hit.block_pos.y, hit.block_pos.z) : 0;
    if (ctx.state.attack_cooldown > 0) {
        --ctx.state.attack_cooldown;
    }
    // ── T-M2: a mob in front of the block turns a left click into an ATTACK ──
    // The same "nearest hit wins" rule the base game applies to a click, and the
    // reason the pick above compares distances. Digging is suspended while a mob
    // is in front (its crack progress is reset), so aiming at a cow cannot also
    // chew through the block behind it.
    gam::MiningTickResult mining_tick;
    if (mob_in_front) {
        ctx.mining.reset();
        ctx.state.crack_stage = -1;
        if (left_held && ctx.state.attack_cooldown == 0) {
            gam::ActionRequest attack;
            attack.kind = gam::ActionKind::Attack;
            attack.target = {static_cast<int>(ctx.state.picked_mob), 0, 0};
            attack.actor = actor_pose(ctx);
            const gam::ActionResult attack_result = ctx.authority.submit(attack);
            if (attack_result.accepted) {
                ctx.state.attack_cooldown = 12; // ⚖ docs/01 §4: ~1.6 swings/s
                ctx.state.swinging = true;
                ctx.state.swing_start = glfwGetTime();
                OC_LOG_INFO("attacked mob {} at ({:.2f}, {:.2f}, {:.2f})", ctx.state.picked_mob,
                            ctx.curr_state.position.x, ctx.curr_state.position.y, ctx.curr_state.position.z);
            } else {
                OC_LOG_WARN("attack refused on mob {}: {}", ctx.state.picked_mob, attack_result.reason());
            }
        }
    } else {
        mining_tick = ctx.mining.tick(hit.block_pos, target_id, hit.hit, left_held);
    }
    if (!mob_in_front && left_held && hit.hit && !ctx.state.swinging) {
        ctx.state.swinging = true;
        ctx.state.swing_start = glfwGetTime();
    }
    if (mining_tick.broke) {
        // T-A1: the break is now a REQUEST. The authoritative side re-checks
        // the target (in reach, chunk loaded, breakable) and performs the write
        // itself; the client keeps only what is presentation - the crack reset
        // and the break particles below.
        gam::ActionRequest dig;
        dig.kind = gam::ActionKind::Dig;
        dig.target = hit.block_pos;
        dig.actor = actor_pose(ctx);
        const gam::ActionResult dig_result = ctx.authority.submit(dig);
        if (!dig_result.accepted) {
            OC_LOG_WARN("dig refused at ({}, {}, {}): {}", hit.block_pos.x, hit.block_pos.y, hit.block_pos.z,
                        dig_result.reason());
        }
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
    // (docs/01 §4). Item use (the vessels) must NOT: a placed source is
    // immediately pickable again, so a held button alternates pour -> scoop
    // (observed on-machine: pour then scoop 4 ticks later), which reads as a
    // broken item. Item use is therefore edge-triggered.
    const bool item_use = is_vessel_use(ctx.state.selected_use);
    const bool use_edge = !ctx.state.prev_right;
    // ── T-M2: feeding the picked mob ─────────────────────────────────────────
    // The food test is local (the client reads the same roster the authority
    // does) so a right click with a block in hand still places a block rather
    // than asking to feed with the wrong item. The authority re-checks the item,
    // the reach, adulthood and the cooldown - this only decides WHICH verb.
    bool feeding = false;
    if (mob_in_front && right_held && use_edge) {
        const srv::Entity *picked = ctx.world.entities().find(ctx.state.picked_mob);
        const gam::MobDef *picked_def = picked != nullptr ? ctx.world.mobs().find(picked->type) : nullptr;
        const std::uint16_t held = ctx.state.selected_stack.item;
        if (picked_def != nullptr && held != gam::ItemRegistry::kEmptyId && held == picked_def->tempt_item) {
            gam::ActionRequest feed;
            feed.kind = gam::ActionKind::Feed;
            feed.target = {static_cast<int>(ctx.state.picked_mob), 0, 0};
            feed.item_or_block = held;
            feed.actor = actor_pose(ctx);
            const gam::ActionResult feed_result = ctx.authority.submit(feed);
            if (feed_result.accepted) {
                // The stack is spent only after the verdict (the T-A1 ordering):
                // a refused feed must not eat the item.
                if (ctx.state.inventory.remove_from_slot(ctx.state.selected_slot, 1) == 1) {
                    ctx.state.refresh_selection();
                }
                ctx.state.swinging = true;
                ctx.state.swing_start = glfwGetTime();
                OC_LOG_INFO("fed mob {} with item {}", ctx.state.picked_mob, held);
            } else {
                OC_LOG_WARN("feed refused on mob {}: {}", ctx.state.picked_mob, feed_result.reason());
            }
            feeding = true;
            ctx.state.place_cooldown = 4; // the same retry rhythm the block path uses
            ctx.state.swinging = true;
            ctx.state.swing_start = glfwGetTime();
        }
    }
    if (!feeding && right_held && (ctx.state.place_cooldown == 0 || !ctx.state.prev_right)) {
        if (hit.hit && (!item_use || use_edge)) {
            if (ctx.state.selected_use == ItemUse::PourVessel) {
                // ── pour (T-F1) ──────────────────────────────────────────
                // A held water vessel pours a source into the cell the hit
                // face opens onto. The authority owns the rule the client used
                // to apply here: the replaceable test only, skipping
                // check_placement's player-overlap test on purpose, because
                // fluids are non-solid and pouring water at your own feet is
                // legal (MC does the same).
                const glm::ivec3 cell = gam::placement_cell(hit);
                gam::ActionRequest pour;
                pour.kind = gam::ActionKind::PourWater;
                pour.target = cell;
                pour.actor = actor_pose(ctx);
                const gam::ActionResult pour_result = ctx.authority.submit(pour);
                if (pour_result.accepted) {
                    // A water vessel is always a 1-stack (max_stack 1), so the
                    // pour swaps it in place and the player is still holding a
                    // container afterwards. The branch below is unreachable in
                    // practice and only reports it if that ever changes.
                    if (!transform_vessel(ctx.state.inventory, ctx.state.selected_slot, ctx.state.vessels.full,
                                          ctx.state.vessels.empty)) {
                        OC_LOG_WARN("vessel: poured water but kept the water vessel (slot {})",
                                    ctx.state.selected_slot);
                    }
                    ctx.state.refresh_selection();
                    OC_LOG_INFO("vessel: poured water source at ({}, {}, {})", cell.x, cell.y, cell.z);
                } else {
                    OC_LOG_WARN("pour refused at ({}, {}, {}): {}", cell.x, cell.y, cell.z, pour_result.reason());
                }
            } else if (ctx.state.selected_use == ItemUse::FillVessel) {
                // ── scoop (T-F1) ─────────────────────────────────────────
                // Check the swap fits BEFORE asking for the write: taking a
                // source and then finding nowhere to put the vessel would
                // destroy one. "Is there a source here" is the authority's
                // call, and it only happens once the swap is known to fit -
                // the same short-circuit the client used to have.
                gam::ActionRequest scoop;
                scoop.kind = gam::ActionKind::ScoopWater;
                scoop.target = hit.block_pos;
                scoop.actor = actor_pose(ctx);
                if (can_transform_vessel(ctx.state.inventory, ctx.state.selected_slot, ctx.state.vessels.empty,
                                         ctx.state.vessels.full)) {
                    const gam::ActionResult scoop_result = ctx.authority.submit(scoop);
                    if (scoop_result.accepted) {
                        transform_vessel(ctx.state.inventory, ctx.state.selected_slot, ctx.state.vessels.empty,
                                         ctx.state.vessels.full);
                        ctx.state.refresh_selection();
                        OC_LOG_INFO("vessel: filled from ({}, {}, {})", hit.block_pos.x, hit.block_pos.y,
                                    hit.block_pos.z);
                    } else {
                        OC_LOG_WARN("scoop refused at ({}, {}, {}): {}", hit.block_pos.x, hit.block_pos.y,
                                    hit.block_pos.z, scoop_result.reason());
                    }
                }
            } else if (ctx.state.selected_block != gam::kNoBlock) {
                // ── place one unit of the held stack ─────────────────────
                // The placement rules themselves (cell replaceable, no actor
                // overlap) moved to the authoritative side with T-A1, so the
                // client no longer validates anything of its own: it names the
                // cell and the block it is holding, and spends the unit only
                // after the request came back accepted. Consuming after the
                // verdict is what makes a refusal unable to eat an item.
                const glm::ivec3 cell = gam::placement_cell(hit);
                gam::ActionRequest place;
                place.kind = gam::ActionKind::PlaceBlock;
                place.target = cell;
                place.item_or_block = ctx.state.selected_block;
                place.actor = actor_pose(ctx);
                const gam::ActionResult place_result = ctx.authority.submit(place);
                if (place_result.accepted) {
                    // place_one_block() reads the block while the cell still
                    // holds it and hands back the block plus the counts, so the
                    // world write and the log never consult the post-refresh
                    // cache (which is the kNoBlock sentinel once the last unit
                    // is gone -- passing that to the block registry throws).
                    const PlacementResult placed = place_one_block(ctx.state.inventory, ctx.state.selected_slot);
                    if (placed.consumed == 1) {
                        ctx.state.refresh_selection();
                        OC_LOG_INFO("placed {} at ({}, {}, {}); consumed 1 from slot {} ({} left)",
                                    ctx.world.registry().string_of(placed.block), cell.x, cell.y, cell.z,
                                    ctx.state.selected_slot, placed.left);
                    }
                } else {
                    OC_LOG_WARN("place refused at ({}, {}, {}): {}", cell.x, cell.y, cell.z, place_result.reason());
                }
            }
        }
        ctx.state.place_cooldown = 4; // ⚖ retry rhythm whether or not the attempt succeeded
        ctx.state.swinging = true;
        ctx.state.swing_start = glfwGetTime();
    }
    ctx.state.prev_right = right_held;

    // ── item pickup (T-E1) ───────────────────────────────────────────────
    // The drop belongs to the authority, the inventory is still the client's
    // (T-A1 kept it out of the request vocabulary; M3 moves it). So the split
    // is: the client reads the drops from its read-only view, filters them with
    // the SAME predicate the authority applies (game/pickup.hpp - one copy, the
    // kReachDistance arrangement), and asks only for the ones that (a) are in
    // the box and (b) fit.
    //
    // Two orderings are load-bearing:
    //   * the candidate list is snapshotted before any submission, because
    //     submitting mutates the store - the visited slot can be freed and
    //     handed to another drop;
    //   * the inventory is modified on a COPY first and committed after the
    //     verdict, so ⚖ 装不下就留在地上 (research/11 §4.2) needs no rollback
    //     and the inventory only ever moves on the accepted side of a request
    //     (the rule T-A1 established for placing a block).
    {
        const gam::ActorPose pose = actor_pose(ctx);
        const srv::EntityStore &drops = ctx.world.entities();
        const gam::EntityTypeRegistry &types = ctx.world.entity_types();
        std::vector<std::pair<srv::EntityId, gam::ItemStack>> candidates;
        drops.for_each_entity([&](const srv::Entity &drop) {
            if (drop.pickup_delay > 0) {
                return; // ⚖ 10 ticks for a natural drop, research/11 §4.2
            }
            const auto [box_min, box_max] = srv::entity_box_corners(drop, types.def_of(drop.type));
            if (!gam::pickup_box_contains(pose, box_min, box_max)) {
                return;
            }
            candidates.emplace_back(drop.id, drop.stack);
        });
        for (const auto &[id, stack] : candidates) {
            if (stack.empty()) {
                continue;
            }
            gam::Inventory next_inventory = ctx.state.inventory;
            gam::ItemStack remainder = stack;
            if (next_inventory.add_item(remainder).remaining != 0) {
                continue; // the drop stays on the ground until there is room
            }
            gam::ActionRequest pick;
            pick.kind = gam::ActionKind::PickUp;
            pick.target = {static_cast<int>(id), 0, 0};
            pick.item_or_block = stack.item;
            pick.actor = pose;
            if (!ctx.authority.submit(pick).accepted) {
                continue; // refused: the drop stays where it is, nothing changed
            }
            ctx.state.inventory = std::move(next_inventory);
            // The hotbar count and the held-item cache follow the pickup, so
            // the HUD shows the new count on this frame (the pickup has no
            // other feedback in this card - 不做库存拾取的 UI 反馈).
            ctx.state.refresh_selection();
        }
    }

    // ── hotbar selection (keys 1..9 select the 9 inventory hotbar cells; the
    //    key-0 bucket slot is gone, the vessels are ordinary items now) ─────
    for (int slot = 0; slot < kHotbarSlots; ++slot) {
        if (key_pressed(ctx.window, GLFW_KEY_1 + slot)) {
            ctx.state.select_slot(slot);
        }
    }

    // ── fluid scheduled ticks (T-F1): one step per game tick, exactly like
    //    the rest of the simulation. The authority advances the fluid layer and
    //    pushes back every chunk it made stale; the client only collects them
    //    for the remesh pass (the same list the actions above pushed into).
    // T-M2: the mob AI is the first system that has to notice the player without
    // an action happening, so the pose goes over once per logic tick, before the
    // authoritative step reads it.
    ctx.authority.observe_actor(actor_pose(ctx));
    ctx.authority.tick();
    const gam::WorldChanges changes = ctx.authority.take_changes();
    ctx.dirty_chunks.insert(ctx.dirty_chunks.end(), changes.dirty_chunks.begin(), changes.dirty_chunks.end());

    // ── what the mobs did to the player (T-M2) ───────────────────────────────
    // The authority simulates the mobs but does not own the player's hit points
    // (T-A1 kept the player out of its vocabulary), so the damage arrives as an
    // event and is applied here, where the health that the HUD renders lives.
    // ⚠ Deliberately NOT modelled: hurt invulnerability, armour, knockback and
    // death - the player-combat card's work. Health floors at 0, and a player at
    // 0 keeps playing (there is no death state yet).
    for (const gam::ActorEvent &event : changes.actor_events) {
        if (event.amount <= 0.0) {
            continue;
        }
        ctx.curr_state.health = std::max(0.0, ctx.curr_state.health - event.amount);
        OC_LOG_INFO("{} for {:.1f} at ({:.2f}, {:.2f}, {:.2f}); health now {:.1f}",
                    event.kind == gam::ActorEventKind::Explosion ? "explosion" : "mob melee", event.amount,
                    event.position.x, event.position.y, event.position.z, ctx.curr_state.health);
    }

    // ── autosave cadence: 200 ticks = ~10 s of game time (T009) ──────────
    if (ctx.save.maybe_autosave_tick()) {
        // The window in request form (T-D4): the same streaming request the
        // frame loop sends, without a generation budget, plus the persist flag.
        // The release window travels with it, so the resident set also tightens
        // on ticks that generate nothing.
        gam::StreamRequest autosave = make_stream_request(ctx.curr_state.position);
        autosave.persist = true;
        const gam::StreamResult result = ctx.authority.stream(autosave);
        ctx.save.write_level_now(make_level_data(ctx));
        OC_LOG_INFO("autosave: {} chunk(s) queued for async write, level written (ticks={})", result.persisted_chunks,
                    ctx.game_ticks);
    }
}

} // namespace opencraft::client
