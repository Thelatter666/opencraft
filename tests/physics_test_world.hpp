#pragma once

#include <vector>

#include "opencraft/physics/block_source.hpp"

namespace physics_test {

// Hand-built axis-aligned box world for headless physics tests. Regions are
// INCLUSIVE integer block ranges on every axis; solid regions win over
// nothing (they are queried independently of liquids, and a test world never
// overlaps the two).
class BoxWorld final : public opencraft::physics::IBlockSource {
public:
    BoxWorld &solid(int x0, int x1, int y0, int y1, int z0, int z1) {
        solids_.push_back(Box{x0, x1, y0, y1, z0, z1});
        return *this;
    }

    BoxWorld &liquid(int x0, int x1, int y0, int y1, int z0, int z1) {
        liquids_.push_back(Box{x0, x1, y0, y1, z0, z1});
        return *this;
    }

    bool solid_at(int wx, int wy, int wz) const override {
        for (const auto &b : solids_) {
            if (wx >= b.x0 && wx <= b.x1 && wy >= b.y0 && wy <= b.y1 && wz >= b.z0 && wz <= b.z1) {
                return true;
            }
        }
        return false;
    }

    bool liquid_at(int wx, int wy, int wz) const override {
        for (const auto &b : liquids_) {
            if (wx >= b.x0 && wx <= b.x1 && wy >= b.y0 && wy <= b.y1 && wz >= b.z0 && wz <= b.z1) {
                return true;
            }
        }
        return false;
    }

private:
    struct Box {
        int x0, x1, y0, y1, z0, z1;
    };

    std::vector<Box> solids_;
    std::vector<Box> liquids_;
};

// Flat ground whose top surface is `top_y` (one solid layer underneath),
// large enough that tests never reach an edge unless they look for one.
inline BoxWorld flat_world(int top_y = 64) {
    BoxWorld w;
    w.solid(-256, 256, top_y - 1, top_y - 1, -256, 256);
    return w;
}

} // namespace physics_test
