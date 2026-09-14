#include <cstdio>
#include "opencraft/voxel/block_registry.hpp"
#include "opencraft/voxel/chunk_manager.hpp"
#include "opencraft/voxel/light_engine.hpp"
#include "opencraft/voxel/light_world_adapter.hpp"
#include "opencraft/worldgen/terrain_generator.hpp"
using namespace opencraft;
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
    // spawn column (8, 8): find surface
    voxel::Chunk &c = *mgr.find(0, 0);
    int surf = 0;
    for (int y = 383; y >= 0; --y) {
        if (c.get_block(8, y, 8) != 0 && reg.def_of(c.get_block(8, y, 8)).solid) { surf = y; break; }
    }
    printf("surface block y=%d id=%u\n", surf, c.get_block(8, surf, 8));
    for (int y = surf; y <= surf + 3; ++y) {
        auto lv = light.light_at(8, y, 8);
        printf("light_at(8,%d,8) sky=%u block=%u\n", y, lv.sky, lv.block);
    }
    // side neighbor air cell example
    auto lv2 = light.light_at(9, surf + 1, 8);
    printf("light_at(9,%d,8) sky=%u block=%u\n", surf + 1, lv2.sky, lv2.block);
    return 0;
}
