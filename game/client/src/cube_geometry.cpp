#include "cube_geometry.hpp"

namespace opencraft::client {

CubeGeometry build_cube_geometry() {
    CubeGeometry geo;
    const glm::vec3 c[8] = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}, {0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}};
    static constexpr int kEdges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                          {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (int e = 0; e < 12; ++e) {
        geo.edge_vertices[e * 2] = c[kEdges[e][0]];
        geo.edge_vertices[e * 2 + 1] = c[kEdges[e][1]];
    }
    static constexpr int kFaces[6][4] = {{4, 5, 6, 7}, {0, 3, 2, 1}, {1, 5, 6, 2},
                                         {3, 7, 6, 2}, {0, 4, 7, 3}, {0, 1, 5, 4}};
    static constexpr glm::vec2 kQuadUv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    // Two triangles per face (non-indexed): 6 faces x 6 verts = 36, matching
    // the glDrawArrays(GL_TRIANGLES, 0, 36) crack overlay call.
    for (int f = 0; f < 6; ++f) {
        static constexpr int kTriOrder[6] = {0, 1, 2, 0, 2, 3};
        for (int v = 0; v < 6; ++v) {
            geo.face_vertices[f * 6 + v] = c[kFaces[f][kTriOrder[v]]];
            geo.face_uv[f * 6 + v] = kQuadUv[kTriOrder[v]];
        }
    }
    return geo;
}

} // namespace opencraft::client
