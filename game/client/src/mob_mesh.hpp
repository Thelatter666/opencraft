#pragma once

// T-B1: voxels -> one triangle list, per joint (docs/research/12-mob-model-
// formats.md §2.1, §4.3, §6.3).
//
// Pure geometry: no GL, no IO. Two jobs:
//   * culled meshing - only faces with no voxel behind them become triangles;
//   * grouping - the vertices of one joint are one contiguous range, so the
//     renderer draws a joint with a single glDrawArrays over that range
//     (§6.3's "the simpler of the two options").
//
// Winding is CCW seen from outside, so the mob pass can leave GL_CULL_FACE ON
// and a 730-face model costs 730 triangles instead of 1460 (§6.6).

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "mob_model.hpp"

namespace opencraft::client {

// One triangle-list vertex: model-space position plus the palette cell to
// sample. 20 bytes, tightly packed, matching the attribute setup in
// mob_render.cpp.
struct MobVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

// A joint's slice of `MobMesh::vertices`, in vertices (always a multiple of
// 6). Only joints that actually own voxels appear, ascending.
struct MobPartRange {
    std::uint8_t joint = 0;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

struct MobMesh {
    std::vector<MobVertex> vertices;
    std::vector<MobPartRange> parts;
    // Pivot per joint, in the same model space as the vertices, so a joint's
    // transform is translate(p) * rotate * translate(-p).
    std::array<glm::vec3, kMobJointCount> pivots{};
    // Occupied height in voxels and the uniform voxels->blocks scale that
    // `world_height` produced, kept for the tests and the startup log.
    float voxel_height = 0.0f;
    float scale = 1.0f;
};

// Builds the mesh for `model`, sized so the occupied voxels are `world_height`
// blocks tall (the mob's collision height - the model then matches whatever the
// simulation says the mob's box is) and anchored at the FEET: the model's
// bottom-centre sits at the entity's position, which is the convention
// Entity::position already uses for every entity kind.
//
// MagicaVoxel's z is up and its y points away from the default camera; the
// model is therefore read as world (x, y, z) = (vx, vz, -vy) - a rotation, so
// the winding below stays CCW-from-outside instead of turning into a mirror.
//
// `world_height` <= 0 falls back to 1.
[[nodiscard]] MobMesh build_mob_mesh(const VoxModel &model, float world_height);

// The joint a voxel's colorIndex belongs to: 1..8 are joint labels (research/12
// §4.3), anything else has no label and is drawn as part of the body.
[[nodiscard]] std::uint8_t mob_joint_of_color(std::uint8_t color_index);

} // namespace opencraft::client
