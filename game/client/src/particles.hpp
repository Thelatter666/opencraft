#pragma once

// Break particles (T009). Moved verbatim out of main.cpp by T-M1 (pure code
// motion); the simulation half lives here, the GL point-cloud draw stays at
// the call site.

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace opencraft::client {

struct Particle {
    glm::vec3 pos;
    glm::vec3 vel;
    float life;      // seconds until removal
    glm::vec4 color; // rgb + current alpha (fades with life)
};

inline constexpr int kParticlesPerBreak = 20;
inline constexpr std::size_t kMaxParticles = 256;

// One frame of particle integration; expired particles are dropped.
void update_particles(std::vector<Particle> &particles, float dt);

} // namespace opencraft::client
