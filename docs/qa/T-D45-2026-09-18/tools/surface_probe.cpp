// T-D45 evidence helper: the surface profile of the saved world's seed, read
// from the SAME worldgen the client uses (no guessing about where to drop the
// player from for the death scene).
#include <cstdio>
#include <cstdlib>
#include "opencraft/game/protocol.hpp"
#include "opencraft/sim/world_sim.hpp"

int main(int argc, char **argv) {
    // The seed stored in the save the evidence run created (override with argv[1]).
    const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 0) : 5715144129572389190ULL;
    opencraft::server::WorldSim sim(seed);
    opencraft::game::StreamRequest req;
    req.generate_radius = 2;
    req.unload_radius = 2;
    req.generate_budget = 25;
    sim.stream(req);
    for (int z = -8; z <= 8; z += 8) {
        for (int x = -32; x <= 40; x += 4) {
            std::printf("z=%d x=%d surface=%d\n", z, x, sim.surface_height(x, z));
        }
    }
    return 0;
}
