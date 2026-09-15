#pragma once

// Unit-cube geometry for the overlays (selection wireframe, crack overlay,
// held item). Moved verbatim out of main.cpp by T-M1 (pure code motion).

#include <array>

#include <glm/glm.hpp>

namespace opencraft::client {

// Unit cube geometry for the overlays: 12 edges (wireframe) and 6 quads with
// uv corners (crack overlay), all in [0,1]^3.
struct CubeGeometry {
    std::array<glm::vec3, 24> edge_vertices;
    std::array<glm::vec3, 36> face_vertices;
    std::array<glm::vec2, 36> face_uv;
};

[[nodiscard]] CubeGeometry build_cube_geometry();

} // namespace opencraft::client
