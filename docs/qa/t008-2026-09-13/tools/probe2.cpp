#include <cstdio>
#include <glm/glm.hpp>
#include "opencraft/voxel/block_registry.hpp"
#include "opencraft/voxel/chunk_manager.hpp"
#include "opencraft/voxel/light_engine.hpp"
#include "opencraft/voxel/light_world_adapter.hpp"
#include "opencraft/worldgen/terrain_generator.hpp"
#include "opencraft/render/mesher.hpp"
using namespace opencraft;

class Src final : public render::IBlockSource {
public:
    Src(voxel::ChunkManager &m) : m_(m) {}
    std::uint16_t block_at(int wx, int wy, int wz) const override {
        if (wy < 0 || wy >= 384) return 0;
        auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
        voxel::Chunk *c = m_.find(cx, cz);
        if (!c) return 0;
        return c->get_block(wx - cx * 16, wy, wz - cz * 16);
    }
private:
    voxel::ChunkManager &m_;
};

int main() {
    voxel::BlockRegistry reg = voxel::BlockRegistry::create_default();
    voxel::ChunkManager mgr;
    voxel::ChunkLightWorld lw(mgr, reg, {});
    voxel::LightEngine light(lw);
    worldgen::TerrainGenerator gen(0x4F50454E43524146ULL, reg);
    for (int cx = -1; cx <= 1; ++cx)
        for (int cz = -1; cz <= 1; ++cz) {
            voxel::Chunk &c = mgr.get_or_load(cx, cz);
            gen.generate_chunk(cx, cz, c);
            light.init_chunk(cx, cz);
        }
    Src src(mgr);
    render::MeshData mesh = render::build_chunk_mesh(src, render::ChunkPos{0, 0});
    // replicate shade_mesh_with_light
    int examined = 0;
    int hist[16] = {0};
    int top_shown = 0;
    auto &bucket = mesh.opaque;
    for (size_t quad = 0; quad + 3 < bucket.vertices.size() ; quad += 4) {
        auto &v0 = bucket.vertices[quad]; auto &v1 = bucket.vertices[quad+1]; auto &v2 = bucket.vertices[quad+2];
        glm::vec3 e1(v1.x-v0.x, v1.y-v0.y, v1.z-v0.z);
        glm::vec3 e2(v2.x-v0.x, v2.y-v0.y, v2.z-v0.z);
        glm::vec3 n = glm::cross(e1, e2);
        int axis = 0;
        for (int a = 1; a < 3; ++a) if (std::abs(n[a]) > std::abs(n[axis])) axis = a;
        int sign = n[axis] > 0.0f ? 1 : -1;
        auto vc = [&](const render::MeshVertex &v){ return axis==0?v.x:(axis==1?v.y:v.z); };
        int plane = vc(v0);
        int bc[3] = {v0.x, v0.y, v0.z};
        for (int a = 0; a < 3; ++a) {
            if (a == axis) bc[a] = sign > 0 ? plane - 1 : plane;
            else bc[a] = std::min({vc(bucket.vertices[quad]), vc(bucket.vertices[quad+1]), vc(bucket.vertices[quad+2]), vc(bucket.vertices[quad+3])});
        }
        int air[3] = {bc[0] + (axis==0?sign:0), bc[1] + (axis==1?sign:0), bc[2] + (axis==2?sign:0)};
        auto lv = light.light_at(air[0], air[1], air[2]);
        int s = lv.sky;
        if (air[1] < 0 || air[1] >= 384) s = air[1] >= 384 ? 15 : 0;
        hist[s]++;
        if (air[1] > 130 && air[1] < 145 && top_shown < 3) printf("NEAR-SURFACE quad air=(%d,%d,%d) axis=%d sign=%d sky=%u\n", air[0], air[1], air[2], axis, sign, lv.sky);
        if (axis == 1 && sign > 0 && v0.y > 130 && top_shown < 5) {
            printf("TOP v0=(%d,%d,%d) bc=(%d,%d,%d) air=(%d,%d,%d) sky=%u\n", v0.x, v0.y, v0.z, bc[0], bc[1], bc[2], air[0], air[1], air[2], lv.sky);
            ++top_shown;
        }
        ++examined;
    }
    printf("quads=%d\n", examined);
    for (int i = 0; i <= 15; ++i) printf("sky %2d: %d\n", i, hist[i]);
    return 0;
}
