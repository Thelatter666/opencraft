#include "mob_pose.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace opencraft::client {

namespace {

// How far a mob has to be off the ground before its legs stop cycling.
constexpr double kMobAirSpeedCutoff = 1e-4;

} // namespace

MobPose mob_pose(const MobPoseInput &in, float phase) {
    MobPose pose;
    // Copied, not derived (§4.4): no interpolation, no "look at the player"
    // fallback, no smoothing - whatever the simulation says is what is drawn.
    pose.yaw = static_cast<float>(in.yaw);
    pose.pitch = static_cast<float>(in.pitch);

    const double speed = std::abs(in.speed);
    pose.scale = in.baby ? kMobBabyScale : 1.0f;

    if (!in.on_ground || speed <= kMobAirSpeedCutoff) {
        // Standing still, or airborne: the cycle does not advance (the clock
        // handles that) and the limbs rest. A mob sliding to a halt keeps its
        // last phase, which is what makes the stop look like a stop.
        pose.swing_x[kMobJointLegLeft] = 0.2f;
        pose.swing_x[kMobJointLegRight] = -0.2f;
        pose.swing_x[kMobJointArmLeft] = -0.05f;
        pose.swing_x[kMobJointArmRight] = -0.05f;
    } else {
        // Shared amplitude: both legs and the body read the same swing, so a
        // faster mob visibly strides wider rather than just faster.
        const float amplitude = kMobStrideRadians * static_cast<float>(std::min(1.0, speed / kMobFullStrideSpeed));
        pose.swing_x[kMobJointLegLeft] = amplitude * std::sin(phase);
        pose.swing_x[kMobJointLegRight] = amplitude * std::sin(phase + 3.14159265f);
        // Arms counter-swing against the legs on the same side.
        pose.swing_x[kMobJointArmLeft] = -amplitude * kMobArmSwingFactor * std::sin(phase);
        pose.swing_x[kMobJointArmRight] = -amplitude * kMobArmSwingFactor * std::sin(phase + 3.14159265f);
        // One bob per stride: two footfalls, so cos of the doubled phase.
        pose.body_offset.y = kMobBodyBobBlocks * 0.5f * (1.0f - std::cos(2.0f * phase)) *
                             static_cast<float>(std::min(1.0, speed / kMobFullStrideSpeed));
    }

    // The head keeps the simulation's pitch. view_dir() looks DOWN for a
    // positive pitch and a positive rotation about +X lifts the model's -Z
    // front, so the head angle is the negated pitch.
    pose.swing_x[kMobJointHead] = -pose.pitch;
    // The tail sways at half the stride: a slow idle sway while walking, and a
    // single slow wave while the body swings.
    pose.swing_y[kMobJointTail] = kMobTailRadians * std::sin(0.5f * phase);

    if (in.hurt_cooldown > 0) {
        // 受击闪红 (research/12 §4.1 A3): the WINDOW is the simulation's
        // hurt_cooldown, only the color is ours.
        pose.tint = glm::vec4(1.0f, 0.35f, 0.35f, 1.0f);
    }
    if (in.fuse > 0) {
        // 蓄爆预告 (A4): pulse with the fuse the simulation is counting down.
        const float burning =
            static_cast<float>(std::clamp(in.fuse, 0, kMobFuseTicks)) / static_cast<float>(kMobFuseTicks);
        const float pulse = 1.0f + 0.12f * burning * std::sin(static_cast<float>(std::max(in.fuse, 0)) * 0.6f);
        pose.scale *= pulse;
        pose.tint = glm::vec4(1.0f, 1.0f - 0.35f * burning, 1.0f - 0.6f * burning, 1.0f);
    }
    return pose;
}

float MobAnimClock::advance(std::uint32_t entity_id, double speed, float dt) {
    Entry &entry = phases_[entity_id];
    entry.frame = frame_;
    if (dt > 0.0f) {
        // Phase advances with DISTANCE TRAVELLED, not with time: a stopped mob
        // freezes mid-stride instead of walking on the spot.
        entry.phase += static_cast<float>(std::abs(speed)) * dt * kMobPhasePerBlock;
        // Keep it bounded, so a long session cannot lose float precision.
        constexpr float kTwoPi = 6.2831853f;
        if (entry.phase > kTwoPi) {
            entry.phase -= kTwoPi * std::floor(entry.phase / kTwoPi);
        }
    }
    return entry.phase;
}

void MobAnimClock::end_frame() {
    ++frame_;
    for (auto it = phases_.begin(); it != phases_.end();) {
        if (it->second.frame + 1 != frame_) {
            it = phases_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace opencraft::client
