#pragma once

// Per-block "main color" lookup used by the break particles (T009). Moved
// verbatim out of main.cpp by T-M1 (pure code motion).

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "atlas.hpp"

namespace opencraft::client {

// Average color of each block's side tile (slot 1) - the "main color" used
// by break particles (T009). Returns one RGB triple per registry id.
[[nodiscard]] std::vector<glm::vec3> block_main_colors(const AtlasImage &atlas, std::size_t registry_size);

} // namespace opencraft::client
