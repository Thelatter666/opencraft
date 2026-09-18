#pragma once

// T-B1: the procedural skeleton's per-frame pose (docs/research/12-mob-model-
// formats.md §4.2 F3, §4.4, §4.5).
//
// The rule this file exists to keep (§4.4, "★ 本节的硬要求"): anything that
// changes WHERE the mob is, WHICH WAY it faces or WHAT it is doing must come
// from the simulation and is copied through unchanged. Only the SHAPE of the
// pose at a fixed position and facing may be derived here. Concretely:
//   * yaw / pitch are copied out of Entity::ai, never computed, smoothed or
//     interpolated - MobPose::yaw == MobPoseInput::yaw exactly (unit-tested);
//   * the walk phase has no simulation source (§1.3), so it is this client's
//     own clock - but it is ADVANCED BY the simulated horizontal speed, so a
//     mob that moves faster swings faster and a mob that stands still stops
//     walking instead of moon-walking on the spot;
//   * hurt / fuse / baby are read from the simulation and only ever change the
//     tint and the scale.
//
// Every amplitude below is uncalibrated (research/12 §4.5 lists "摆动频率" and
// "摆动幅度" as 待校准, with no source). They sit in named constants so the
// tuning card has one place to edit.

#include <array>
#include <cstdint>
#include <unordered_map>

#include <glm/glm.hpp>

#include "mob_model.hpp"

namespace opencraft::client {

enum MobJoint : std::uint8_t {
    kMobJointBody = 0,
    kMobJointHead = 1,
    kMobJointArmLeft = 2,
    kMobJointArmRight = 3,
    kMobJointLegLeft = 4,
    kMobJointLegRight = 5,
    kMobJointTail = 6,
    kMobJointSpare = 7,
};

// Simulation values, and nothing else. Filled straight from the Entity by the
// mob pass.
struct MobPoseInput {
    double yaw = 0.0;      // Entity::ai::yaw - the only source of facing
    double pitch = 0.0;    // Entity::ai::pitch - same sign convention as view_dir()
    double speed = 0.0;    // |Entity::velocity| in the XZ plane, blocks/tick
    bool on_ground = true; // Entity::on_ground
    int hurt_cooldown = 0; // Entity::ai::hurt_cooldown - > 0 while flashing
    int fuse = 0;          // Entity::ai::fuse - > 0 while a Blastbud's fuse burns
    bool baby = false;     // Entity::ai::baby
};

// What the renderer applies. Angles are radians about the joint's own local
// axes (X = the limb swing plane, Y = the tail's sway).
struct MobPose {
    float yaw = 0.0f;
    float pitch = 0.0f;          // head only, and only this far
    glm::vec3 body_offset{0.0f}; // local bob
    std::array<float, kMobJointCount> swing_x{};
    std::array<float, kMobJointCount> swing_y{};
    float scale = 1.0f;
    glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
};

// Locomotion tuning. All 待校准 (research/12 §4.5).
inline constexpr float kMobStrideRadians = 0.62f;  // leg swing at full speed
inline constexpr float kMobArmSwingFactor = 0.55f; // arms swing less than legs
inline constexpr float kMobBodyBobBlocks = 0.035f; // vertical bob on a full stride
inline constexpr float kMobTailRadians = 0.22f;    // tail sway
inline constexpr float kMobBabyScale = 0.55f;      // 幼体 (render-side; the sim has only the flag)
inline constexpr int kMobFuseTicks = 30;           // ⚖ docs/01 §6: 蓄爆 30 tick

// The speed at which the walk cycle is fully extended, in blocks/tick. The
// passive mobs' attribute speed is well under this, so the swing stays partial
// rather than clamping for the whole game.
inline constexpr double kMobFullStrideSpeed = 0.16;

// Walk-cycle phase gained per block travelled: 2*pi over a ~0.8 block stride,
// i.e. the limbs are back where they started after the mob has walked 0.8
// blocks. 待校准 (research/12 §4.5 has no source for the frequency).
inline constexpr float kMobPhasePerBlock = 7.85f;

// The pose for one instance at one instant. `phase` is the walk cycle in
// radians, advanced by MobAnimClock.
[[nodiscard]] MobPose mob_pose(const MobPoseInput &in, float phase);

// Per-instance walk phase. `EntityId` is stable while the entity lives, and a
// slot is recycled with a fresh id, so a stale entry can only ever be re-read
// by its own entity; end_frame() still drops the ones that stopped being drawn.
class MobAnimClock {
public:
    // Advances this instance's phase by the distance it just travelled, so the
    // cycle is a function of motion: no motion, no advance.
    float advance(std::uint32_t entity_id, double speed, float dt);

    // Forgets every instance that was not advanced this frame.
    void end_frame();

private:
    struct Entry {
        float phase = 0.0f;
        std::uint64_t frame = 0;
    };

    std::unordered_map<std::uint32_t, Entry> phases_;
    std::uint64_t frame_ = 0;
};

} // namespace opencraft::client
