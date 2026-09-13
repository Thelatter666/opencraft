#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include "opencraft/voxel/chunk.hpp"
#include "opencraft/voxel/light_storage.hpp"

namespace opencraft::voxel {

// The two block properties lighting cares about. Injected as an interface so
// LightEngine is unit-testable without a ChunkManager (T006 contract).
struct BlockLightProps {
    bool transparent = true;   // light may enter / pass through
    std::uint8_t emission = 0; // 0-15 self-emitted blocklight
};

class ILightWorld {
public:
    virtual ~ILightWorld() = default;

    // Properties of the block at a world position. May be called for positions
    // whose chunk has no light storage yet; implementations must answer
    // without side effects (an unloaded chunk is typically air).
    [[nodiscard]] virtual BlockLightProps props_at(int world_x, int world_y, int world_z) const = 0;

    // Properties by block id, used by on_block_changed for the old/new ids.
    [[nodiscard]] virtual BlockLightProps props_of(std::uint16_t block_id) const = 0;
};

// Dual-channel lighting engine: 15-level skylight (column direct hit 15,
// no attenuation straight down, -1 in every other direction) and blocklight
// (BFS from emitters, -1 per step). Opaque blocks block both channels.
//
// Light data lives in per-chunk LightStorage instances owned by the engine.
// All propagation uses explicit queues (no recursion, docs/research/03 §2);
// removal uses the darkness BFS with re-lighting of independently lit
// boundary cells. Propagation attempts into chunks without light storage are
// recorded as deduplicated offers and replayed when that chunk is
// initialized, so init order does not matter.
//
// Single-threaded by design; callers serialize access (worker-pool wiring is
// a later integration task).
class LightEngine {
public:
    explicit LightEngine(const ILightWorld &world);

    // Computes initial light for the chunk: direct skylight columns, emitter
    // scan, boundary pull from already-initialized neighbors, and replay of
    // deferred cross-chunk offers. Re-initializing an existing chunk resets
    // its light from scratch.
    void init_chunk(int chunk_x, int chunk_z);

    // Incremental update entry point. Call AFTER the world data already
    // reflects the change (props_at must return the new state). No-op when
    // the chunk has not been initialized yet (init_chunk will catch up).
    void on_block_changed(int world_x, int world_y, int world_z, std::uint16_t old_id, std::uint16_t new_id);

    // {sky, block} at a world position; {0, 0} when the chunk has no light
    // storage (not initialized) or y is outside 0..383.
    [[nodiscard]] LightLevels light_at(int world_x, int world_y, int world_z) const;

    // Diagnostics: number of deferred cross-chunk offers waiting for their
    // target chunk to be initialized. 0 once every involved chunk is init'ed.
    [[nodiscard]] std::size_t pending_count() const;

    [[nodiscard]] bool chunk_initialized(int chunk_x, int chunk_z) const;

    // ── unload hook (T006 follow-up, wired by T009) ─────────────────────────
    // Drops the chunk's light storage and any deferred cross-chunk offers
    // targeting it. Call when a chunk leaves memory so a later re-init cannot
    // trip over stale per-chunk light data or replay offers that no longer
    // match a reloaded world.
    void forget_chunk(int chunk_x, int chunk_z);

private:
    struct Node {
        int x;
        int y;
        int z;
        std::uint8_t value;
    };

    enum class Channel : std::uint8_t { Sky, Block };

    [[nodiscard]] LightStorage *storage_for(int world_x, int world_z);
    [[nodiscard]] const LightStorage *storage_for(int world_x, int world_z) const;

    // Drains the add queue for one channel until convergence. Targets in
    // chunks without storage are recorded as pending offers instead.
    void propagate(Channel channel);

    // Darkness BFS: strips `seed_value` starting at the seed cell (the caller
    // has NOT cleared it yet; darken() does). Cells whose light is independent
    // of the removed path (or that emit their own) are re-queued as add seeds.
    void darken(Channel channel, int world_x, int world_y, int world_z, std::uint8_t seed_value);

    // Records a deferred propagation into a not-yet-initialized chunk.
    void record_offer(int world_x, int world_y, int world_z, Channel channel, std::uint8_t value);

    void enqueue(Node node, Channel channel);
    void drain_queues();

    // Deferred cross-chunk offers, keyed by target chunk, then by
    // (local position, channel), keeping the maximum offered value. Replay
    // validates against the (possibly changed) live source value, so keeping
    // the max is safe.
    using OfferMap = std::unordered_map<std::uint32_t, std::uint8_t>;
    std::unordered_map<std::int64_t, OfferMap> pending_;

    std::deque<Node> sky_add_;
    std::deque<Node> block_add_;

    const ILightWorld &world_;
    std::unordered_map<std::int64_t, LightStorage> storages_;
};

} // namespace opencraft::voxel
