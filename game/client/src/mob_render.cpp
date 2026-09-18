#include "mob_render.hpp"

#include <cstddef>
#include <utility>

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace opencraft::client {

namespace {

// The atlas owns unit 0 and the font unit 1 (main.cpp); the palette takes the
// next free one so the chunk pass, which binds the atlas once at startup and
// never again, keeps sampling what it thinks it is sampling.
constexpr int kPaletteTextureUnit = 2;

// A palette is a 16x16 RGBA8 texture: one pixel per entry, the same shape as a
// block tile, so it goes through the same loader (asset_atlas.hpp).
constexpr int kPaletteTextureSide = 16;

// Uploads one model. VertexArray/Buffer/Texture2D all own a GL name and none is
// default-constructible, so the resources are built here and aggregate-
// initialised into the struct.
//
// ⚠ render::Texture2D's constructor binds on the ACTIVE texture unit (it never
// calls glActiveTexture itself). The atlas is bound to unit 0 once at startup
// and never rebound, so uploading a palette while unit 0 happens to be active
// silently replaces the atlas with a 16x16 palette - and the whole world then
// samples black, with the model itself drawn correctly on top of it. Selecting
// the palette's own unit first is what keeps the two channels apart.
MobGpuModel make_gpu_model(const MobModelAsset &asset, std::size_t voxels) {
    render::VertexArray vao;
    vao.bind();
    render::Buffer vbo(render::Buffer::Target::Vertex, asset.mesh.vertices.data(),
                       asset.mesh.vertices.size() * sizeof(MobVertex), render::Buffer::Usage::Static);
    vbo.bind();
    vao.set_attribute(0, 3, GL_FLOAT, sizeof(MobVertex), offsetof(MobVertex, x));
    vao.set_attribute(1, 2, GL_FLOAT, sizeof(MobVertex), offsetof(MobVertex, u));
    glActiveTexture(GL_TEXTURE0 + static_cast<unsigned int>(kPaletteTextureUnit));
    render::Texture2D palette(kPaletteTextureSide, kPaletteTextureSide, asset.palette.data());
    glActiveTexture(GL_TEXTURE0);
    return MobGpuModel{std::move(vao),
                       std::move(vbo),
                       std::move(palette),
                       asset.mesh.parts,
                       asset.mesh.pivots,
                       asset.mesh.vertices.size() / 3,
                       voxels};
}

} // namespace

MobRenderer::MobRenderer(const std::vector<MobModelAsset> &assets, const render::Shader &shader) : shader_(&shader) {
    models_.reserve(assets.size());
    for (const MobModelAsset &asset : assets) {
        models_.emplace_back(asset.id, make_gpu_model(asset, asset.voxels));
    }
}

bool MobRenderer::has(std::string_view mob_id) const {
    for (const auto &entry : models_) {
        if (entry.first == mob_id) {
            return true;
        }
    }
    return false;
}

void MobRenderer::begin_pass() const {
    // See the header: the two-box fallback leaves this OFF (mirrored winding),
    // so the model pass cannot assume what it wants is already set - and §4.3
    // says every pass sets its own state anyway.
    glEnable(GL_CULL_FACE);
}

void MobRenderer::end_pass() const {
    // Back to the state the rest of the frame runs with (main.cpp enables
    // GL_CULL_FACE once before the loop and every pass restores it).
    glEnable(GL_CULL_FACE);
}

void MobRenderer::draw(std::string_view mob_id, const glm::mat4 &view_projection, const glm::mat4 &model_matrix,
                       const MobPose &pose) const {
    const MobGpuModel *model = nullptr;
    for (const auto &entry : models_) {
        if (entry.first == mob_id) {
            model = &entry.second;
            break;
        }
    }
    if (model == nullptr) {
        return;
    }

    shader_->use();
    glUniform1i(shader_->uniform_location("u_palette"), kPaletteTextureUnit);
    model->palette.bind(kPaletteTextureUnit);
    glUniform4fv(shader_->uniform_location("u_tint"), 1, glm::value_ptr(pose.tint));
    model->vao.bind();

    // The mesh is anchored at the feet and centred horizontally, so the model
    // matrix carries position + facing (both simulation values) and the
    // presentation-only scale.
    const glm::mat4 base = model_matrix * glm::scale(glm::mat4(1.0f), glm::vec3(pose.scale));

    for (const MobPartRange &part : model->parts) {
        const glm::vec3 pivot = model->pivots[part.joint];
        glm::mat4 joint = glm::translate(glm::mat4(1.0f), pivot);
        if (pose.swing_x[part.joint] != 0.0f) {
            joint = glm::rotate(joint, pose.swing_x[part.joint], glm::vec3(1.0f, 0.0f, 0.0f));
        }
        if (pose.swing_y[part.joint] != 0.0f) {
            joint = glm::rotate(joint, pose.swing_y[part.joint], glm::vec3(0.0f, 1.0f, 0.0f));
        }
        joint = glm::translate(joint, -pivot);
        if (part.joint == kMobJointBody) {
            joint = glm::translate(joint, pose.body_offset);
        }
        const glm::mat4 mvp = view_projection * base * joint;
        glUniformMatrix4fv(shader_->uniform_location("u_mvp"), 1, GL_FALSE, glm::value_ptr(mvp));
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(part.first), static_cast<GLsizei>(part.count));
    }
}

} // namespace opencraft::client
