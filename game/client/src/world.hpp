#pragma once

// T-A1: the client's world object is a READ-ONLY VIEW over the authoritative
// world (server::WorldSim). It has no write method at all, so "the client
// cannot change the world" is enforced by the compiler rather than by
// convention:
//
//   * reads      -> this view, for the renderer / physics / raycast / HUD;
//   * actions    -> game::IAuthority::submit(), carried by TickContext;
//   * push-back  -> game::IAuthority::take_changes(), the stale-chunk list.
//
// It keeps implementing every query interface the engine layers ask through
// (block queries for meshing and raycasting, solid/liquid queries for
// step_player, fluid surfaces for the water layer), so the renderer, the
// physics and the HUD are unchanged: they still take a `const WorldSource &`.

#include <cstdint>

#include "opencraft/physics/block_source.hpp"
#include "opencraft/render/mesher.hpp"
#include "opencraft/sim/world_sim.hpp"

namespace opencraft::client {

// The client's view of the authoritative world. Every member is a const
// forwarder: this class holds no state of its own.
class WorldSource final : public render::IBlockSource, public physics::IBlockSource, public render::IFluidSource {
public:
    explicit WorldSource(const server::WorldSim &authority) : authority_(&authority) {}

    // ── what the renderer / HUD / mining rules ask for ──────────────────────
    [[nodiscard]] const voxel::BlockRegistry &registry() const { return authority_->registry(); }

    [[nodiscard]] const voxel::LightEngine &light() const { return authority_->light(); }

    // Block id the fluid layer uses as its in-world placeholder.
    [[nodiscard]] std::uint16_t water_block_id() const { return authority_->water_block_id(); }

    // True when the chunk is in memory (main.cpp's remesh/streaming loops).
    [[nodiscard]] bool chunk_ready(int cx, int cz) const { return authority_->chunk_ready(cx, cz); }

    // True when the chunk and its 4 side neighbors are generated - meshing a
    // chunk before that would bake wrong (air) border faces.
    [[nodiscard]] bool neighbors_ready(int cx, int cz) const { return authority_->neighbors_ready(cx, cz); }

    // ── entities (T-E1) ─────────────────────────────────────────────────────
    // Same const-forwarder shape as the block registry above, and the same
    // reason: the drop store is authoritative state, so the client gets a view
    // of it, not a copy. It has no write method, so "the client cannot move a
    // drop" is a compile error rather than a convention.
    [[nodiscard]] const server::EntityStore &entities() const { return authority_->entities(); }

    // Per-type entity parameters - the box the pickup filter needs, and the
    // block-form lookup the dropped-item renderer needs.
    [[nodiscard]] const game::EntityTypeRegistry &entity_types() const { return authority_->entity_types(); }

    // The item registry (T-E1 moved it to the authority: it decides what a
    // broken block drops, so the item id space is the world's).
    [[nodiscard]] const game::ItemRegistry &items() const { return authority_->items(); }

    // The mob roster (T-M2): the renderer reads a mob's body from its
    // EntityDef, and the tick asks whether the held item is that species' food.
    [[nodiscard]] const game::MobRegistry &mobs() const { return authority_->mobs(); }

    // ── render::IBlockSource (meshing + voxel raycast) ──────────────────────
    [[nodiscard]] std::uint16_t block_at(int wx, int wy, int wz) const override {
        return authority_->block_at(wx, wy, wz);
    }

    // ── render::IFluidSource (water surface height for the mesher) ----------
    [[nodiscard]] float fluid_height_at(int wx, int wy, int wz) const override {
        return authority_->fluid_height_at(wx, wy, wz);
    }

    [[nodiscard]] render::FluidSpan fluid_span(int cx, int cz) const override { return authority_->fluid_span(cx, cz); }

    // ── physics::IBlockSource (step_player) ---------------------------------
    // Unloaded chunks read as solid: the player cannot walk/fall into the
    // streaming void.
    [[nodiscard]] bool solid_at(int wx, int wy, int wz) const override { return authority_->solid_at(wx, wy, wz); }

    [[nodiscard]] bool liquid_at(int wx, int wy, int wz) const override { return authority_->liquid_at(wx, wy, wz); }

private:
    const server::WorldSim *authority_;
};

// Applies the T008 lighting rule to a freshly built mesh: per face-quad,
// sample sky/block light at the air cell the face opens into and fold it
// into the T005 directional shade byte:
//     brightness = max(sky * day_factor, block) / 15, day_factor = 1.0
// with a small ambient floor so caves stay barely visible. Quad normals are
// recovered from the CCW winding, so the mesher stays untouched.
void shade_mesh_with_light(render::MeshData &mesh, const WorldSource &world, int cx, int cz);

} // namespace opencraft::client
