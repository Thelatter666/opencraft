#include "opencraft/sim/world_sim.hpp"

#include <algorithm>
#include <cmath>

#include "opencraft/core/log.hpp"
#include "opencraft/game/pickup.hpp"
#include "opencraft/game/placement.hpp"
#include "opencraft/physics/player_state.hpp"

namespace opencraft::server {

namespace {

// Visits every fluid cell of a chunk, skipping sections without fluid storage.
template <typename Fn>
void for_each_fluid_cell(const voxel::Chunk &chunk, Fn &&visit) {
    for (int section = 0; section < voxel::Chunk::kSectionCount; ++section) {
        if (chunk.fluid_section_empty(section)) {
            continue;
        }
        const int base_y = section * voxel::Chunk::kSectionSize;
        for (int ly = 0; ly < voxel::Chunk::kSectionSize; ++ly) {
            for (int lz = 0; lz < voxel::Chunk::kSizeZ; ++lz) {
                for (int lx = 0; lx < voxel::Chunk::kSizeX; ++lx) {
                    if (!chunk.get_fluid(lx, base_y + ly, lz).empty()) {
                        visit(lx, base_y + ly, lz);
                    }
                }
            }
        }
    }
}

// Slack on the reach comparison, in blocks: the client's ray stops when the
// entry distance exceeds the reach, so a legitimate hit sits exactly on the
// boundary when it lands flush. The epsilon keeps IEEE rounding from refusing
// a request the client's own aim accepted.
constexpr double kReachEpsilon = 1e-6;

// Chebyshev distance, in chunks. Both streaming windows are squares (the shape
// the client's offset loop has always used), so the hysteresis band is the same
// width in all four directions; a circular test would leave diagonal chunks with
// less slack than the cardinal ones.
[[nodiscard]] int chunk_distance(int cx, int cz, int center_cx, int center_cz) {
    const int dx = cx > center_cx ? cx - center_cx : center_cx - cx;
    const int dz = cz > center_cz ? cz - center_cz : center_cz - cz;
    return dx > dz ? dx : dz;
}

} // namespace

WorldSim::WorldSim(const std::uint64_t seed)
    : registry_(voxel::BlockRegistry::create_default()), light_world_(chunks_, registry_, {}), light_(light_world_),
      generator_(seed, registry_), fluid_world_(*this), fluid_(fluid_world_),
      items_(game::ItemRegistry::create_default()), entity_types_(game::EntityTypeRegistry::create_default()),
      entity_world_(*this), item_hazard_(registry_) {
    water_block_id_ = registry_.id_of("water");
}

// ── command channel ─────────────────────────────────────────────────────────

game::ActionResult WorldSim::submit(const game::ActionRequest &req) {
    switch (req.kind) {
    case game::ActionKind::Dig:
        return apply_dig(req);
    case game::ActionKind::PlaceBlock:
        return apply_place(req);
    case game::ActionKind::PourWater:
        return apply_pour(req);
    case game::ActionKind::ScoopWater:
        return apply_scoop(req);
    case game::ActionKind::PickUp:
        return apply_pickup(req);
    }
    return {false, game::ActionReject::UnknownBlock}; // unreachable: every kind is handled above
}

void WorldSim::tick() {
    fluid_.step();
    // T-E1: the entity layer advances on the same authoritative step, in its own
    // system plane (docs/03 §6). It runs before the early-out below: the drop
    // simulation has nothing to do with whether the fluid made a chunk stale.
    step_items(entities_, entity_world_, item_hazard_, item_rules_, entity_types_, items_);
    if (fluid_dirty_.empty()) {
        return;
    }
    std::sort(fluid_dirty_.begin(), fluid_dirty_.end());
    fluid_dirty_.erase(std::unique(fluid_dirty_.begin(), fluid_dirty_.end()), fluid_dirty_.end());
    pending_chunks_.insert(pending_chunks_.end(), fluid_dirty_.begin(), fluid_dirty_.end());
    fluid_dirty_.clear();
}

game::WorldChanges WorldSim::take_changes() {
    game::WorldChanges changes;
    changes.dirty_chunks.swap(pending_chunks_);
    return changes;
}

// ── action rules ────────────────────────────────────────────────────────────

game::ActionReject WorldSim::gate(const game::ActionRequest &req) const {
    if (req.target.y < 0 || req.target.y >= voxel::Chunk::kSizeY) {
        return game::ActionReject::OutOfWorld;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(req.target.x, req.target.z);
    if (chunks_.find(cx, cz) == nullptr) {
        return game::ActionReject::ChunkNotLoaded;
    }
    return in_reach(req) ? game::ActionReject::None : game::ActionReject::OutOfReach;
}

bool WorldSim::in_reach(const game::ActionRequest &req) const {
    // Distance from the eye to the CELL, not to its centre: a ray reaches the
    // cell by entering it, so the nearest point of the cube is never further
    // than the hit the client aimed with. The placement cell (one block past
    // the hit) shares the face the ray crossed, so it passes the same test.
    const glm::dvec3 eye = req.actor.feet + glm::dvec3(0.0, req.actor.eye_height, 0.0);
    const glm::dvec3 lower(req.target);
    const glm::dvec3 nearest = glm::clamp(eye, lower, lower + glm::dvec3(1.0));
    return glm::length(eye - nearest) <= game::kReachDistance + kReachEpsilon;
}

game::ActionResult WorldSim::apply_dig(const game::ActionRequest &req) {
    if (const game::ActionReject reject = gate(req); reject != game::ActionReject::None) {
        return {false, reject};
    }
    const std::uint16_t id = block_at(req.target.x, req.target.y, req.target.z);
    if (id == 0 || !registry_.has_numeric(id)) {
        return {false, game::ActionReject::NothingToDig};
    }
    // Hardness < 0 is the registry's unbreakable marker (bedrock). The same
    // rule the mining state machine applies; re-checked here because the
    // authority cannot take the client's word for when a block breaks.
    if (registry_.def_of(id).hardness < 0.0f) {
        return {false, game::ActionReject::Unbreakable};
    }
    write_block(req.target.x, req.target.y, req.target.z, 0);
    // T-E1: a broken block leaves its item behind (research/11 §4.1-4.4). The
    // drop is authoritative state: it is spawned HERE, by the same call that
    // removed the block, so "the block is gone" and "its drop exists" cannot
    // drift apart. A block with no item form (water - T-I1 ruling S-3) simply
    // leaves nothing.
    const EntityId drop = spawn_item_drop(entities_, entity_types_, items_, item_rules_, req.target, id);
    if (drop != EntityStore::kNoEntity) {
        // ★ The ONE resident log this card adds (the card allows exactly one,
        // and names this site: a successful dig had no resident log at all, so
        // "did a drop appear" could only be answered by a temporary probe). It
        // carries the item, the count, the entity id and the spawn position -
        // everything the on-machine acceptance needs to tie a screenshot of a
        // drop to the dig that produced it.
        const Entity *entity = entities_.find(drop);
        OC_LOG_INFO("item drop {} spawned at ({:.2f}, {:.2f}, {:.2f}) [entity {}]",
                    items_.string_of(entity->stack.item), entity->position.x, entity->position.y, entity->position.z,
                    drop);
    }
    return {true, game::ActionReject::None};
}

game::ActionResult WorldSim::apply_place(const game::ActionRequest &req) {
    if (const game::ActionReject reject = gate(req); reject != game::ActionReject::None) {
        return {false, reject};
    }
    // Air is not a placement; an unknown id would throw inside the registry.
    if (req.item_or_block == 0 || !registry_.has_numeric(req.item_or_block)) {
        return {false, game::ActionReject::UnknownBlock};
    }
    const game::PlacementStatus status = game::check_placement(registry_, *this, req.target, req.actor.feet,
                                                               req.actor.height, physics::PlayerState::kHalfWidth);
    if (status == game::PlacementStatus::CellOccupied) {
        return {false, game::ActionReject::CellOccupied};
    }
    if (status == game::PlacementStatus::IntersectsPlayer) {
        return {false, game::ActionReject::IntersectsActor};
    }
    write_block(req.target.x, req.target.y, req.target.z, req.item_or_block);
    return {true, game::ActionReject::None};
}

game::ActionResult WorldSim::apply_pour(const game::ActionRequest &req) {
    if (const game::ActionReject reject = gate(req); reject != game::ActionReject::None) {
        return {false, reject};
    }
    // No actor-overlap test: fluids are non-solid, so pouring water at your own
    // feet is legal (the same rule the client-side block placement skips).
    const std::uint16_t occupant = block_at(req.target.x, req.target.y, req.target.z);
    if (!game::is_replaceable(registry_, occupant)) {
        return {false, game::ActionReject::CellOccupied};
    }
    if (!place_water_source(req.target.x, req.target.y, req.target.z)) {
        return {false, game::ActionReject::ChunkNotLoaded};
    }
    return {true, game::ActionReject::None};
}

game::ActionResult WorldSim::apply_scoop(const game::ActionRequest &req) {
    if (const game::ActionReject reject = gate(req); reject != game::ActionReject::None) {
        return {false, reject};
    }
    // "Is there a source here" stays inside remove_water_source(): one
    // implementation of the rule, not two.
    if (!remove_water_source(req.target.x, req.target.y, req.target.z)) {
        return {false, game::ActionReject::NotAWaterSource};
    }
    return {true, game::ActionReject::None};
}

game::ActionResult WorldSim::apply_pickup(const game::ActionRequest &req) {
    // T-E1. The gate() rules are deliberately NOT reused: a drop is not a block
    // cell, so `target.y` bounds and ⚖ block reach say nothing about it. The
    // checks below are the whole of the pickup rule, and each one is a reason
    // the client cannot decide for itself.
    if (req.target.x < 0) {
        return {false, game::ActionReject::UnknownEntity};
    }
    Entity *drop = entities_.find(static_cast<EntityId>(req.target.x));
    if (drop == nullptr) {
        return {false, game::ActionReject::UnknownEntity};
    }
    // The client names the item it read from its view. The store reuses slots,
    // so this is what stops a stale id from handing over somebody else's item.
    if (req.item_or_block != drop->stack.item) {
        return {false, game::ActionReject::EntityItemMismatch};
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(static_cast<int>(std::floor(drop->position.x)),
                                                     static_cast<int>(std::floor(drop->position.z)));
    if (!chunk_ready(cx, cz)) {
        // A drop in an unloaded chunk is frozen - its timers do not run, so it
        // is not pickable either (research/11 §4.4). In-process unreachable
        // (the client cannot see such a drop), kept because the rule is the
        // authority's and M3 puts a network in front of it.
        return {false, game::ActionReject::EntityNotLoaded};
    }
    if (drop->pickup_delay > 0) {
        return {false, game::ActionReject::PickupDelayActive};
    }
    const game::EntityDef &def = entity_types_.def_of(drop->type);
    const auto [box_min, box_max] = entity_box_corners(*drop, def);
    if (!game::pickup_box_contains(req.actor, box_min, box_max)) {
        return {false, game::ActionReject::OutOfPickupRange};
    }
    // The whole stack goes, or nothing does: the client only asks when its own
    // dry run says the stack fits, and MC's partial fill (what does not fit
    // stays on the ground) is left to the card that needs it. The client read
    // the stack BEFORE this call, the same read-then-spend ordering
    // place_one_block uses.
    entities_.erase(drop->id);
    return {true, game::ActionReject::None};
}

// ── world lifecycle ─────────────────────────────────────────────────────────

game::StreamResult WorldSim::stream(const game::StreamRequest &req) {
    game::StreamResult result;

    // 1. Generate, nearest-first, up to the caller's frame budget. The order is
    //    the one the client's own offset loop used, so what streams in first is
    //    unchanged; what is new is that the loop can no longer run unbudgeted.
    const int radius = req.generate_radius > 0 ? req.generate_radius : 0;
    if (req.generate_budget > 0) {
        int budget = req.generate_budget;
        for (const auto &[dx, dz] : generate_offsets(radius)) {
            if (budget == 0) {
                break;
            }
            const int cx = req.center_cx + dx;
            const int cz = req.center_cz + dz;
            if (chunks_.find(cx, cz) != nullptr) {
                continue; // already resident: re-generating it would throw the edits away
            }
            if (ensure_chunk(cx, cz)) {
                --budget;
                result.loaded_chunks.emplace_back(cx, cz);
            }
        }
    }

    // 2. Release what left the retention window. The sweep walks the resident
    //    set rather than a ring around the centre, so a chunk that streamed in
    //    around a previous position (the startup square, a teleported player)
    //    is released too. unload_chunk persists the dirty ones before dropping
    //    their state - that is the whole point of the ordering.
    const int keep = req.unload_radius > 0 ? req.unload_radius : 0;
    std::vector<std::pair<int, int>> leaving;
    for (const auto &[cx, cz] : resident_) {
        if (chunk_distance(cx, cz, req.center_cx, req.center_cz) > keep) {
            leaving.emplace_back(cx, cz);
        }
    }
    // resident_ is already sorted, but say it out loud: the result feeds logs
    // and tests, and their order must not depend on the container.
    std::sort(leaving.begin(), leaving.end());
    for (const auto &[cx, cz] : leaving) {
        if (unload_chunk(cx, cz)) {
            result.unloaded_chunks.emplace_back(cx, cz);
        }
    }

    // 3. The persistence window, in request form (the T009 cadence pass).
    if (req.persist) {
        result.persisted_chunks = autosave_pass();
    }

    result.loaded_total = chunks_.loaded_count();
    return result;
}

const std::vector<std::pair<int, int>> &WorldSim::generate_offsets(int radius) {
    if (gen_offsets_radius_ == radius) {
        return gen_offsets_;
    }
    gen_offsets_.clear();
    gen_offsets_.reserve(static_cast<std::size_t>(2 * radius + 1) * static_cast<std::size_t>(2 * radius + 1));
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            gen_offsets_.emplace_back(dx, dz);
        }
    }
    std::sort(gen_offsets_.begin(), gen_offsets_.end(), [](const auto &a, const auto &b) {
        return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
    });
    gen_offsets_radius_ = radius;
    return gen_offsets_;
}

bool WorldSim::ensure_chunk(int cx, int cz) {
    voxel::Chunk &chunk = chunks_.get_or_load(cx, cz);
    // Detect a fresh chunk: get_or_load hands out an empty one when absent.
    // (A generated chunk is never empty - bedrock floor is always present.)
    if (!chunk.empty()) {
        return false;
    }
    // Disk-first (T009): persisted blocks win over regeneration. Light is
    // NOT persisted (the documented choice) - init_chunk recomputes it.
    if (save_ != nullptr) {
        if (const std::optional<std::vector<std::uint8_t>> payload = save_->load_chunk(cx, cz); payload.has_value()) {
            core::ByteBuffer buffer;
            buffer.write_bytes(payload->data(), payload->size());
            buffer.rewind();
            chunk = voxel::Chunk::deserialize(buffer);
            light_.init_chunk(cx, cz);
            wake_fluid_at_chunk_border(cx, cz);
            OC_LOG_INFO("chunk ({}, {}) loaded from disk", cx, cz);
            return mark_resident(cx, cz);
        }
    }
    generator_.generate_chunk(cx, cz, chunk);
    light_.init_chunk(cx, cz);
    wake_fluid_at_chunk_border(cx, cz);
    return mark_resident(cx, cz);
}

bool WorldSim::neighbors_ready(int cx, int cz) const {
    return chunk_ready(cx, cz) && chunk_ready(cx - 1, cz) && chunk_ready(cx + 1, cz) && chunk_ready(cx, cz - 1) &&
           chunk_ready(cx, cz + 1);
}

int WorldSim::surface_height(int wx, int wz) const {
    const voxel::Chunk *chunk = chunks_.find_world(wx, wz);
    if (chunk == nullptr) {
        return 0;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const int lx = wx - cx * voxel::Chunk::kSizeX;
    const int lz = wz - cz * voxel::Chunk::kSizeZ;
    for (int y = voxel::Chunk::kSizeY - 1; y >= 0; --y) {
        const std::uint16_t id = chunk->get_block(lx, y, lz);
        if (id != 0 && registry_.def_of(id).solid) {
            return y + 1;
        }
    }
    return 0;
}

glm::dvec3 WorldSim::find_spawn() {
    // 5x5 column scan around the origin, center-out (T009 card item: spawn
    // on the surface's highest solid block). All 25 columns live in chunk
    // (0, 0), which the caller generated first.
    static constexpr std::pair<int, int> kOffsets[25] = {
        {0, 0},  {1, 0},  {-1, 0},  {0, 1},  {0, -1}, {1, 1},  {1, -1},  {-1, 1},  {-1, -1},
        {2, 0},  {-2, 0}, {0, 2},   {0, -2}, {2, 1},  {2, -1}, {-2, 1},  {-2, -1}, {1, 2},
        {-1, 2}, {1, -2}, {-1, -2}, {2, 2},  {2, -2}, {-2, 2}, {-2, -2},
    };
    for (const auto &[dx, dz] : kOffsets) {
        const int wx = dx;
        const int wz = dz;
        const int surface = surface_height(wx, wz);
        if (surface <= 0 || surface + 1 >= voxel::Chunk::kSizeY) {
            continue;
        }
        const std::uint16_t ground = block_at(wx, surface - 1, wz);
        if (!registry_.def_of(ground).solid || registry_.def_of(ground).liquid) {
            continue;
        }
        // Two air cells above the ground: feet and head.
        if (block_at(wx, surface, wz) != 0 || block_at(wx, surface + 1, wz) != 0) {
            continue;
        }
        return {wx + 0.5, static_cast<double>(surface), wz + 0.5};
    }
    // Scan failed (e.g. the whole area is water): fall back to the legacy
    // spawn column; physics will handle whatever is there.
    return {8.5, static_cast<double>(surface_height(8, 8)), 8.5};
}

bool WorldSim::unload_chunk(int cx, int cz) {
    if (chunks_.find(cx, cz) == nullptr) {
        return false;
    }
    // Persistence before release (T009 card: 卸载时若有脏数据先落盘再释放).
    // Untouched chunks regenerate deterministically, so only modified ones
    // hit the disk (card: 未修改的区块不落盘).
    if (save_ != nullptr && save_->is_dirty(cx, cz)) {
        core::ByteBuffer payload;
        if (serialize_chunk(cx, cz, payload)) {
            save_->store_chunk_sync(cx, cz, payload.data(), payload.size());
        }
    }
    // Light goes with the blocks: forget_chunk also drops the deferred offers
    // addressed to this chunk, so a later re-init cannot replay an offer against
    // a reloaded world (T006's rule; no second mechanism is invented here).
    light_.forget_chunk(cx, cz);
    static_cast<void>(chunks_.unload(voxel::Chunk::chunk_coord(cx * voxel::Chunk::kSizeX, cz * voxel::Chunk::kSizeZ)));
    resident_.erase({cx, cz});
    return true;
}

std::size_t WorldSim::autosave_pass() {
    if (save_ == nullptr) {
        return 0;
    }
    std::size_t written = 0;
    for (const auto &[cx, cz] : save_->take_dirty()) {
        core::ByteBuffer payload;
        if (serialize_chunk(cx, cz, payload)) {
            // Snapshot copied on the main thread; the IO thread only ever
            // sees the byte vector (T009 card concurrency rule).
            save_->store_chunk_async(cx, cz,
                                     std::vector<std::uint8_t>(payload.data(), payload.data() + payload.size()));
            ++written;
        }
        // A dirty-but-unloaded chunk (should not happen: write_block only marks
        // loaded chunks) is simply dropped from the set; its data is whatever
        // disk already holds.
    }
    return written;
}

bool WorldSim::serialize_chunk(int cx, int cz, core::ByteBuffer &out) const {
    const voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return false;
    }
    chunk->serialize(out);
    return true;
}

// ── reads ───────────────────────────────────────────────────────────────────

std::uint16_t WorldSim::block_at(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return 0; // out of world: air (render IBlockSource contract)
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return 0; // unloaded: air, keeps world edges visible
    }
    return chunk->get_block(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ);
}

bool WorldSim::solid_at(int wx, int wy, int wz) const {
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return true; // unloaded reads solid: no falling into the void
    }
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return false;
    }
    return registry_.def_of(chunk->get_block(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ)).solid;
}

bool WorldSim::liquid_at(int wx, int wy, int wz) const {
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr || wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return false;
    }
    return registry_.def_of(chunk->get_block(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ))
        .liquid;
}

float WorldSim::fluid_height_at(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return 0.0f;
    }
    const voxel::Chunk *chunk = chunks_.find_world(wx, wz);
    if (chunk == nullptr) {
        return 0.0f;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const voxel::FluidCell cell = chunk->get_fluid(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ);
    if (cell.empty()) {
        return 0.0f;
    }
    return voxel::fluid_render_height(cell.level);
}

render::FluidSpan WorldSim::fluid_span(int cx, int cz) const {
    const voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return {};
    }
    render::FluidSpan span{};
    for (int section = 0; section < voxel::Chunk::kSectionCount; ++section) {
        if (chunk->fluid_section_empty(section)) {
            continue;
        }
        if (span.empty()) {
            span.first = section;
        }
        span.last = section + 1;
    }
    return span;
}

std::uint16_t WorldSim::fluid_at(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return 0;
    }
    const voxel::Chunk *chunk = chunks_.find_world(wx, wz);
    if (chunk == nullptr) {
        return 0;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    return voxel::pack_fluid(chunk->get_fluid(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ));
}

bool WorldSim::fluid_may_enter(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return false;
    }
    const voxel::Chunk *chunk = chunks_.find_world(wx, wz);
    if (chunk == nullptr) {
        return false; // unloaded chunks are walls for fluid (§4.7 / R-5)
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const std::uint16_t id = chunk->get_block(wx - cx * voxel::Chunk::kSizeX, wy, wz - cz * voxel::Chunk::kSizeZ);
    if (id == 0) {
        return true; // air
    }
    return registry_.has_numeric(id) && registry_.def_of(id).liquid;
}

// ── writers (submit() and the fluid simulation only) ────────────────────────

void WorldSim::write_block(int wx, int wy, int wz, std::uint16_t id) {
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return;
    }
    const int lx = wx - cx * voxel::Chunk::kSizeX;
    const int lz = wz - cz * voxel::Chunk::kSizeZ;
    const std::uint16_t old_id = chunk->get_block(lx, wy, lz);
    if (old_id == id) {
        return;
    }
    chunk->set_block(lx, wy, lz, id);
    light_.on_block_changed(wx, wy, wz, old_id, id);
    // A block edit is a block update for the fluid too: the cell and its
    // neighbours re-evaluate on the fluid's own schedule, which is what lets
    // water pour into a freshly mined hole (docs/research/10 §7.2).
    fluid_.on_block_changed(wx, wy, wz);
    if (save_ != nullptr) {
        save_->mark_dirty(cx, cz);
    }

    // Own chunk plus every side neighbor within light reach of the changed
    // cell (light spreads up to 15 cells horizontally, so a change at lx
    // can re-shade faces in chunk cx+1 unless lx == 15 - the light cannot
    // cross 16 cells). Diagonal neighbors when both axes are in reach.
    auto mark = [this](int x, int z) { pending_chunks_.emplace_back(x, z); };
    mark(cx, cz);
    const bool east = lx >= 1;
    const bool west = lx <= voxel::Chunk::kSizeX - 2;
    const bool south = lz >= 1;
    const bool north = lz <= voxel::Chunk::kSizeZ - 2;
    if (east) {
        mark(cx + 1, cz);
    }
    if (west) {
        mark(cx - 1, cz);
    }
    if (south) {
        mark(cx, cz + 1);
    }
    if (north) {
        mark(cx, cz - 1);
    }
    if (east && south) {
        mark(cx + 1, cz + 1);
    }
    if (east && north) {
        mark(cx + 1, cz - 1);
    }
    if (west && south) {
        mark(cx - 1, cz + 1);
    }
    if (west && north) {
        mark(cx - 1, cz - 1);
    }
}

void WorldSim::set_fluid_at(int wx, int wy, int wz, std::uint16_t cell) {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return; // unloaded: the simulation treats it as a wall anyway
    }
    const int lx = wx - cx * voxel::Chunk::kSizeX;
    const int lz = wz - cz * voxel::Chunk::kSizeZ;
    const voxel::FluidCell unpacked = voxel::unpack_fluid(cell);
    chunk->set_fluid(lx, wy, lz, unpacked);

    // Keep the block layer in step so targeting, mining, physics and
    // persistence all see the water. A cell that loses its fluid only reverts
    // to air when it actually held the placeholder block (a player-placed
    // block on top of water must not be deleted).
    const std::uint16_t old_id = chunk->get_block(lx, wy, lz);
    const std::uint16_t new_id = !unpacked.empty() ? water_block_id_ : (old_id == water_block_id_ ? 0 : old_id);
    if (old_id != new_id) {
        chunk->set_block(lx, wy, lz, new_id);
        light_.on_block_changed(wx, wy, wz, old_id, new_id);
    }
    if (save_ != nullptr) {
        save_->mark_dirty(cx, cz);
    }
    mark_fluid_dirty(wx, wz);
}

void WorldSim::mark_fluid_dirty(int wx, int wz) {
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const int lx = wx - cx * voxel::Chunk::kSizeX;
    const int lz = wz - cz * voxel::Chunk::kSizeZ;
    fluid_dirty_.emplace_back(cx, cz);
    // A cell on a chunk border also changes the neighbour chunk's mesh: the
    // water surface of the neighbour emits a wall against it.
    if (lx == 0) {
        fluid_dirty_.emplace_back(cx - 1, cz);
    }
    if (lx == voxel::Chunk::kSizeX - 1) {
        fluid_dirty_.emplace_back(cx + 1, cz);
    }
    if (lz == 0) {
        fluid_dirty_.emplace_back(cx, cz - 1);
    }
    if (lz == voxel::Chunk::kSizeZ - 1) {
        fluid_dirty_.emplace_back(cx, cz + 1);
    }
}

bool WorldSim::place_water_source(int wx, int wy, int wz) {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return false;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    if (chunks_.find(cx, cz) == nullptr) {
        return false; // unloaded: the write would be dropped
    }
    fluid_.place_source(wx, wy, wz, voxel::FluidKind::Water);
    return true;
}

bool WorldSim::is_water_source(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= voxel::Chunk::kSizeY) {
        return false;
    }
    const voxel::Chunk *chunk = chunks_.find_world(wx, wz);
    if (chunk == nullptr) {
        return false;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const int lx = wx - cx * voxel::Chunk::kSizeX;
    const int lz = wz - cz * voxel::Chunk::kSizeZ;
    const voxel::FluidCell cell = chunk->get_fluid(lx, wy, lz);
    if (!cell.empty()) {
        return cell.source; // flowing water cannot be scooped up, as in MC
    }
    // Worldgen water is not in the fluid layer; treat it as a source so a
    // bucket works on an ocean exactly like it does on a poured source.
    return chunk->get_block(lx, wy, lz) == water_block_id_;
}

bool WorldSim::remove_water_source(int wx, int wy, int wz) {
    if (!is_water_source(wx, wy, wz)) {
        return false;
    }
    const auto [cx, cz] = voxel::Chunk::chunk_coords(wx, wz);
    const voxel::Chunk *chunk = chunks_.find(cx, cz);
    if (chunk == nullptr) {
        return false;
    }
    const int lx = wx - cx * voxel::Chunk::kSizeX;
    const int lz = wz - cz * voxel::Chunk::kSizeZ;
    if (!chunk->get_fluid(lx, wy, lz).empty()) {
        fluid_.clear_cell(wx, wy, wz); // clears the fluid cell and the placeholder block
        return true;
    }
    write_block(wx, wy, wz, 0); // legacy static water block
    fluid_.on_block_changed(wx, wy, wz);
    return true;
}

void WorldSim::wake_fluid_at_chunk_border(int cx, int cz) {
    if (const voxel::Chunk *chunk = chunks_.find(cx, cz); chunk != nullptr) {
        for_each_fluid_cell(*chunk, [&](int lx, int y, int lz) {
            fluid_.wake(cx * voxel::Chunk::kSizeX + lx, y, cz * voxel::Chunk::kSizeZ + lz);
        });
    }
    // Neighbours: only the column facing the new chunk changed neighbourhood.
    static constexpr int kSides[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto &side : kSides) {
        const int nx = cx + side[0];
        const int nz = cz + side[1];
        const voxel::Chunk *chunk = chunks_.find(nx, nz);
        if (chunk == nullptr) {
            continue;
        }
        for_each_fluid_cell(*chunk, [&](int lx, int y, int lz) {
            const bool facing = side[0] != 0 ? lx == (side[0] > 0 ? 0 : voxel::Chunk::kSizeX - 1)
                                             : lz == (side[1] > 0 ? 0 : voxel::Chunk::kSizeZ - 1);
            if (!facing) {
                return;
            }
            fluid_.wake(nx * voxel::Chunk::kSizeX + lx, y, nz * voxel::Chunk::kSizeZ + lz);
        });
    }
}

} // namespace opencraft::server
