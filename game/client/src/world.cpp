#include "world.hpp"

#include <algorithm>
#include <cmath>

// T-A1: this file used to hold the client's world implementation. The world
// (storage, light, fluid, terrain) now lives on the authoritative side -
// server::WorldSim - and the client keeps only the view declared in world.hpp
// plus the mesh shading rule below, which is presentation and stays here.

namespace opencraft::client {

void shade_mesh_with_light(render::MeshData &mesh, const WorldSource &world, int cx, int cz) {
    constexpr float kAmbientFloor = 0.05f;
    constexpr float kDayFactor = 1.0f; // day/night cycle is M2; fixed daylight for T008

    auto shade_bucket = [&](render::MeshBucket &bucket) {
        for (std::size_t quad = 0; quad + 3 < bucket.vertices.size(); quad += 4) {
            const auto &v0 = bucket.vertices[quad];
            const auto &v1 = bucket.vertices[quad + 1];
            const auto &v2 = bucket.vertices[quad + 2];
            const glm::vec3 e1(static_cast<float>(v1.x) - v0.x, static_cast<float>(v1.y) - v0.y,
                               static_cast<float>(v1.z) - v0.z);
            const glm::vec3 e2(static_cast<float>(v2.x) - v0.x, static_cast<float>(v2.y) - v0.y,
                               static_cast<float>(v2.z) - v0.z);
            const glm::vec3 normal = glm::cross(e1, e2);
            // The dominant axis of the cross product is the face normal axis;
            // CCW-from-outside winding makes its sign point out of the block.
            int axis = 0;
            for (int a = 1; a < 3; ++a) {
                if (std::abs(normal[a]) > std::abs(normal[axis])) {
                    axis = a;
                }
            }
            const int sign = normal[axis] > 0.0f ? 1 : -1;

            // Block cell: on the normal axis the face plane sits at the
            // block's outer boundary (plane-1 for a positive normal, plane
            // for a negative one); on the tangent axes it is the min corner.
            const auto comp = [](const render::MeshVertex &v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); };
            const int plane = comp(v0, axis);
            int block_cell[3] = {v0.x, v0.y, v0.z};
            for (int a = 0; a < 3; ++a) {
                if (a == axis) {
                    block_cell[a] = sign > 0 ? plane - 1 : plane;
                } else {
                    // Tangent axis: the min corner over the quad's 4 vertices.
                    block_cell[a] = std::min({comp(bucket.vertices[quad], a), comp(bucket.vertices[quad + 1], a),
                                              comp(bucket.vertices[quad + 2], a), comp(bucket.vertices[quad + 3], a)});
                }
            }
            const int air[3] = {block_cell[0] + (axis == 0 ? sign : 0), block_cell[1] + (axis == 1 ? sign : 0),
                                block_cell[2] + (axis == 2 ? sign : 0)};

            const int wx = cx * render::kChunkSizeX + air[0];
            const int wz = cz * render::kChunkSizeZ + air[2];
            std::uint8_t sky = 15;
            std::uint8_t block_light = 0;
            if (air[1] >= 0 && air[1] < render::kChunkSizeY) {
                const auto levels = world.light().light_at(wx, air[1], wz);
                sky = levels.sky;
                block_light = levels.block;
            }
            const float brightness = std::max(
                kAmbientFloor, std::max(static_cast<float>(sky) * kDayFactor, static_cast<float>(block_light)) / 15.0f);

            for (int c = 0; c < 4; ++c) {
                auto &v = bucket.vertices[quad + static_cast<std::size_t>(c)];
                const int shaded = static_cast<int>(static_cast<float>(v.shade) * brightness + 0.5f);
                v.shade = static_cast<std::uint8_t>(std::clamp(shaded, 0, 255));
            }
        }
    };
    shade_bucket(mesh.opaque);
    shade_bucket(mesh.translucent);
}

} // namespace opencraft::client
