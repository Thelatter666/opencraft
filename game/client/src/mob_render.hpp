#pragma once

// T-B1: the mob model's GL side (docs/research/12-mob-model-formats.md §6.3).
//
// Everything here lives in the client, and engine/render/** is untouched - the
// card freezes it (Q5). Resources are per MOB TYPE, not per instance and not
// per frame: 70 mossbacks share one vertex buffer, and a mob type with no model
// has no GL objects at all (that instance goes down the pre-existing two-box
// code path instead, byte for byte).
//
// State discipline (§6.6): this pass sets everything it depends on - shader,
// VAO, texture unit, cull face - and restores the cull face to the state the
// rest of the frame expects.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "mob_mesh.hpp"
#include "mob_model.hpp"
#include "mob_pose.hpp"

#include "opencraft/render/rhi.hpp"

namespace opencraft::client {

// The CPU-side model, baked: already meshed for its mob's height and already
// carrying the palette it will be drawn with. main() builds these (it is the
// only place that knows both the asset set and the mob definitions).
struct MobModelAsset {
    std::string id;
    MobMesh mesh;
    std::array<std::uint32_t, kMobPaletteSize> palette{};
    std::size_t voxels = 0;
};

// One mob type's GPU resources.
struct MobGpuModel {
    render::VertexArray vao;
    render::Buffer vbo;
    render::Texture2D palette;
    std::vector<MobPartRange> parts;
    std::array<glm::vec3, kMobJointCount> pivots{};
    // Kept so the startup log and the report can quote the geometry without
    // meshing a second time.
    std::size_t triangles = 0;
    std::size_t voxels = 0;
};

// The whole model channel. Construction requires a current GL context and a
// compiled shader; the shader must outlive it.
class MobRenderer {
public:
    MobRenderer(const std::vector<MobModelAsset> &assets, const render::Shader &shader);

    // The routing test of §6.4: false means "this mob has no model, use the
    // two-box path".
    [[nodiscard]] bool has(std::string_view mob_id) const;

    // ── the cull-face pair (§4.2) ───────────────────────────────────────────
    // The two-box path turns GL_CULL_FACE OFF because the overlay cube's
    // winding is mirrored; the model's is not, and a 730-face model drawn with
    // culling off costs twice the triangles. So the model pass re-asserts the
    // frame's baseline on entry and restores it on exit. Call both, once per
    // frame, ONLY when the model pass will draw - a frame with no models must
    // leave GL state exactly as it was.
    void begin_pass() const;
    void end_pass() const;

    // Draws one instance. `model_matrix` already carries the entity's position
    // and yaw (simulation values); `pose` carries the joint angles.
    void draw(std::string_view mob_id, const glm::mat4 &view_projection, const glm::mat4 &model_matrix,
              const MobPose &pose) const;

    [[nodiscard]] std::size_t size() const { return models_.size(); }

private:
    const render::Shader *shader_ = nullptr;
    std::vector<std::pair<std::string, MobGpuModel>> models_;
};

} // namespace opencraft::client
