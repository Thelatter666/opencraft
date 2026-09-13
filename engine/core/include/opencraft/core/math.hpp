#pragma once

#include <glm/glm.hpp>

namespace opencraft::core {

// Axis-aligned bounding box in min/max (inclusive-min, exclusive-max corners)
// representation, matching the voxel grid convention used across the engine.
struct AABB {
    glm::vec3 min{0.0F};
    glm::vec3 max{0.0F};

    // True when the two boxes share at least one point of volume.
    [[nodiscard]] bool overlaps(const AABB &other) const {
        return min.x < other.max.x && max.x > other.min.x && min.y < other.max.y && max.y > other.min.y &&
               min.z < other.max.z && max.z > other.min.z;
    }

    // Point containment (max corner treated as exclusive).
    [[nodiscard]] bool contains(const glm::vec3 &point) const {
        return point.x >= min.x && point.x < max.x && point.y >= min.y && point.y < max.y && point.z >= min.z &&
               point.z < max.z;
    }

    // Full containment of another box.
    [[nodiscard]] bool contains(const AABB &other) const {
        return other.min.x >= min.x && other.max.x <= max.x && other.min.y >= min.y && other.max.y <= max.y &&
               other.min.z >= min.z && other.max.z <= max.z;
    }

    // Grow (or shrink for negative margin) uniformly on all sides.
    [[nodiscard]] AABB expanded(float margin) const { return AABB{min - glm::vec3(margin), max + glm::vec3(margin)}; }

    [[nodiscard]] glm::vec3 size() const { return max - min; }

    [[nodiscard]] glm::vec3 center() const { return (min + max) * 0.5F; }
};

} // namespace opencraft::core
