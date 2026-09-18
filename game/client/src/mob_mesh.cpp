#include "mob_mesh.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace opencraft::client {

namespace {

// One quad per (voxel, face), given as the four corners in CCW order seen from
// outside the cube. Corner coordinates are unit-cube offsets from the voxel's
// model-space minimum corner.
struct FaceTemplate {
    glm::ivec3 corners[4];
    // The neighbour direction in VOXEL space that hides this face.
    glm::ivec3 neighbor_vox;
};

// World +X/-X/+Y/-Y/+Z/-Z, in that order. `neighbor_vox` is the world direction
// mapped back through (x, y, z)_vox -> (x, z, -y)_world.
constexpr std::array<FaceTemplate, 6> kFaces{{
    {{{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, {1, 0, 0}},
    {{{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}}, {-1, 0, 0}},
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {0, 0, 1}},
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {0, 0, -1}},
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {0, -1, 0}},
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {0, 1, 0}},
}};

std::uint32_t cell_key(int x, int y, int z, const VoxModel &model) {
    return (static_cast<std::uint32_t>(x) * static_cast<std::uint32_t>(model.size_y) + static_cast<std::uint32_t>(y)) *
               static_cast<std::uint32_t>(model.size_z) +
           static_cast<std::uint32_t>(z);
}

// The palette cell for colorIndex `color`. The palette array IS the texture
// (palette[c] is uploaded as texel c), so a voxel painted with colorIndex c has
// to sample texel c - not c-1, even though the FILE stores color c at chunk
// entry c-1 and the loader already did that shift when it filled the array.
//
// Sampling the cell CENTRE is exact here - a 16x16 palette gives every entry
// exactly one pixel - and keeps a nearest-filtered texture from picking up a
// neighbour.
glm::vec2 palette_uv(std::uint8_t color) {
    const int col = static_cast<int>(color) % 16;
    const int row = static_cast<int>(color) / 16;
    constexpr float kCells = 16.0f;
    return glm::vec2((static_cast<float>(col) + 0.5f) / kCells, (static_cast<float>(row) + 0.5f) / kCells);
}

} // namespace

std::uint8_t mob_joint_of_color(std::uint8_t color_index) {
    // Palette contract v2 (T-B2b, docs/tasks/T-B2.ruling.md §2): the first
    // SIXTEEN indices are joint labels - 1..8 a joint's primary colour,
    // 9..16 the SECOND colour of joint (idx-1) mod 8 - so one joint can be
    // painted in two colours (eyes on the head) while staying inside the
    // 8-joint draw-call budget. 17..255 are plain colour slots: body group.
    if (color_index >= 1 && color_index <= 2 * kMobJointCount) {
        return static_cast<std::uint8_t>((color_index - 1) % kMobJointCount);
    }
    return 0; // no joint label: part of the body
}

MobMesh build_mob_mesh(const VoxModel &model, float world_height) {
    MobMesh mesh;
    if (model.voxels.empty()) {
        return mesh;
    }

    std::unordered_set<std::uint32_t> occupied;
    occupied.reserve(model.voxels.size() * 2);
    for (const VoxVoxel &v : model.voxels) {
        occupied.insert(cell_key(v.x, v.y, v.z, model));
    }

    // Occupied bounds in model space, as world-oriented voxel coordinates: a
    // voxel at (vx, vy, vz) spans x [vx, vx+1], y [vz, vz+1], z [-(vy+1), -vy].
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(-std::numeric_limits<float>::max());
    for (const VoxVoxel &v : model.voxels) {
        const glm::vec3 base(static_cast<float>(v.x), static_cast<float>(v.z), -static_cast<float>(v.y) - 1.0f);
        lo = glm::min(lo, base);
        hi = glm::max(hi, base + glm::vec3(1.0f));
    }
    const glm::vec3 centre((lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f);
    const float voxel_height = std::max(hi.y - lo.y, 1.0f);
    const float scale = (world_height > 0.0f ? world_height : 1.0f) / voxel_height;

    const auto to_model = [&](const glm::vec3 &voxel_point) {
        return glm::vec3(voxel_point.x - centre.x, voxel_point.y - lo.y, voxel_point.z - centre.z) * scale;
    };

    // Per-joint bounds, for the pivot rule below.
    std::array<glm::vec3, kMobJointCount> part_lo{};
    std::array<glm::vec3, kMobJointCount> part_hi{};
    std::array<bool, kMobJointCount> part_seen{};
    for (const VoxVoxel &v : model.voxels) {
        const std::uint8_t joint = mob_joint_of_color(v.color);
        const glm::vec3 base(static_cast<float>(v.x), static_cast<float>(v.z), -static_cast<float>(v.y) - 1.0f);
        if (!part_seen[joint]) {
            part_lo[joint] = base;
            part_hi[joint] = base + glm::vec3(1.0f);
            part_seen[joint] = true;
        } else {
            part_lo[joint] = glm::min(part_lo[joint], base);
            part_hi[joint] = glm::max(part_hi[joint], base + glm::vec3(1.0f));
        }
    }

    // One joint at a time, so each joint's vertices are one contiguous range
    // (a single glDrawArrays per joint, research/12 §6.3).
    for (std::size_t joint = 0; joint < kMobJointCount; ++joint) {
        if (!part_seen[joint]) {
            continue;
        }
        // Pivot convention, per joint family: hem at the top of a limb
        // (shoulder/hip), neck at the bottom of the head, centre for everything
        // else. The alternative - a pivot per voxel in the file - would need a
        // second authoring channel for one number per joint; this rule needs no
        // data and is documented instead.
        const glm::vec3 part_centre((part_lo[joint].x + part_hi[joint].x) * 0.5f, 0.0f,
                                    (part_lo[joint].z + part_hi[joint].z) * 0.5f);
        glm::vec3 pivot_vox;
        if (joint >= 2 && joint <= 5) {
            pivot_vox = glm::vec3(part_centre.x, part_hi[joint].y, part_centre.z);
        } else if (joint == 1) {
            pivot_vox = glm::vec3(part_centre.x, part_lo[joint].y, part_centre.z);
        } else {
            pivot_vox = glm::vec3(part_centre.x, (part_lo[joint].y + part_hi[joint].y) * 0.5f, part_centre.z);
        }
        mesh.pivots[joint] = to_model(pivot_vox);

        MobPartRange range;
        range.joint = static_cast<std::uint8_t>(joint);
        range.first = static_cast<std::uint32_t>(mesh.vertices.size());

        for (const VoxVoxel &v : model.voxels) {
            const std::uint8_t color = v.color;
            if (mob_joint_of_color(color) != joint) {
                continue;
            }
            const glm::vec3 base(static_cast<float>(v.x), static_cast<float>(v.z), -static_cast<float>(v.y) - 1.0f);
            const glm::vec2 uv = palette_uv(color);
            for (const FaceTemplate &face : kFaces) {
                const int nx = static_cast<int>(v.x) + face.neighbor_vox.x;
                const int ny = static_cast<int>(v.y) + face.neighbor_vox.y;
                const int nz = static_cast<int>(v.z) + face.neighbor_vox.z;
                if (nx >= 0 && ny >= 0 && nz >= 0 && nx < model.size_x && ny < model.size_y && nz < model.size_z &&
                    occupied.count(cell_key(nx, ny, nz, model)) != 0) {
                    continue; // hidden by its neighbour
                }
                const auto emit = [&](const glm::ivec3 &corner) {
                    const glm::vec3 p = to_model(base + glm::vec3(corner));
                    mesh.vertices.push_back(MobVertex{p.x, p.y, p.z, uv.x, uv.y});
                };
                // Quad -> two triangles; the corners are already CCW from
                // outside, so no winding fix-up is needed anywhere.
                emit(face.corners[0]);
                emit(face.corners[1]);
                emit(face.corners[2]);
                emit(face.corners[0]);
                emit(face.corners[2]);
                emit(face.corners[3]);
            }
        }

        range.count = static_cast<std::uint32_t>(mesh.vertices.size()) - range.first;
        if (range.count > 0) {
            mesh.parts.push_back(range);
        }
    }

    mesh.voxel_height = voxel_height;
    mesh.scale = scale;
    return mesh;
}

} // namespace opencraft::client
