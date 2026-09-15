#include "particles.hpp"

#include <algorithm>

namespace opencraft::client {

void update_particles(std::vector<Particle> &particles, float dt) {
    constexpr float kGravity = 13.0f; // blocks/s^2, snappier than real g for feel
    for (Particle &p : particles) {
        p.vel.y -= kGravity * dt;
        p.pos += p.vel * dt;
        p.life -= dt;
        p.color.a = std::clamp(p.life / 0.5f, 0.0f, 1.0f);
    }
    particles.erase(
        std::remove_if(particles.begin(), particles.end(), [](const Particle &p) { return p.life <= 0.0f; }),
        particles.end());
}

} // namespace opencraft::client
