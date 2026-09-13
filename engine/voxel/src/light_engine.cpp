#include "opencraft/voxel/light_engine.hpp"

#include <algorithm>
#include <array>

namespace opencraft::voxel {

namespace {

constexpr std::uint8_t kMaxLight = 15;

// 6-neighborhood; sky uses the downward entry specially (no attenuation from
// a 15 cell, docs/research/03 §2.2).
struct Dir {
    int dx;
    int dy;
    int dz;
};

constexpr std::array<Dir, 6> kDirs{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};

[[nodiscard]] int chunk_base(int chunk_coord_value) {
    return chunk_coord_value * Chunk::kSizeX;
}

} // namespace

LightEngine::LightEngine(const ILightWorld &world) : world_(world) {
}

bool LightEngine::chunk_initialized(int chunk_x, int chunk_z) const {
    return storages_.find(Chunk::chunk_coord(chunk_base(chunk_x), chunk_base(chunk_z))) != storages_.end();
}

void LightEngine::forget_chunk(int chunk_x, int chunk_z) {
    const std::int64_t key = Chunk::chunk_coord(chunk_base(chunk_x), chunk_base(chunk_z));
    storages_.erase(key);
    pending_.erase(key);
}

const LightStorage *LightEngine::storage_for(int world_x, int world_z) const {
    const auto it = storages_.find(Chunk::chunk_coord(world_x, world_z));
    return it == storages_.end() ? nullptr : &it->second;
}

LightStorage *LightEngine::storage_for(int world_x, int world_z) {
    const auto it = storages_.find(Chunk::chunk_coord(world_x, world_z));
    return it == storages_.end() ? nullptr : &it->second;
}

std::size_t LightEngine::pending_count() const {
    std::size_t total = 0;
    for (const auto &entry : pending_) {
        total += entry.second.size();
    }
    return total;
}

LightLevels LightEngine::light_at(int world_x, int world_y, int world_z) const {
    if (world_y < 0 || world_y >= LightStorage::kSizeY) {
        return LightLevels{};
    }
    const LightStorage *storage = storage_for(world_x, world_z);
    if (storage == nullptr) {
        return LightLevels{};
    }
    return storage->levels_world(world_x, world_y, world_z);
}

void LightEngine::enqueue(Node node, Channel channel) {
    if (channel == Channel::Sky) {
        sky_add_.push_back(node);
    } else {
        block_add_.push_back(node);
    }
}

void LightEngine::record_offer(int world_x, int world_y, int world_z, Channel channel, std::uint8_t value) {
    const auto [cx, cz] = Chunk::chunk_coords(world_x, world_z);
    const auto lx = static_cast<std::uint32_t>(world_x - cx * Chunk::kSizeX);
    const auto lz = static_cast<std::uint32_t>(world_z - cz * Chunk::kSizeZ);
    // y needs 9 bits (0..383), x/z 4 bits each, plus 1 channel bit.
    const std::uint32_t packed =
        ((static_cast<std::uint32_t>(world_y) << 9) | (lz << 5) | lx) << 1 | (channel == Channel::Sky ? 1U : 0U);
    auto &offers = pending_[Chunk::chunk_coord(world_x, world_z)];
    auto [it, inserted] = offers.try_emplace(packed, value);
    if (!inserted) {
        it->second = std::max(it->second, value);
    }
}

void LightEngine::propagate(Channel channel) {
    std::deque<Node> &queue = channel == Channel::Sky ? sky_add_ : block_add_;
    while (!queue.empty()) {
        const Node node = queue.front();
        queue.pop_front();
        LightStorage *source = storage_for(node.x, node.z);
        if (source == nullptr) {
            continue;
        }
        const std::uint8_t stored = channel == Channel::Sky ? source->sky_world(node.x, node.y, node.z)
                                                            : source->block_light_world(node.x, node.y, node.z);
        if (stored != node.value) {
            continue; // stale entry: the cell changed since this node was queued
        }
        for (const Dir &dir : kDirs) {
            const int nx = node.x + dir.dx;
            const int ny = node.y + dir.dy;
            const int nz = node.z + dir.dz;
            if (ny < 0 || ny >= LightStorage::kSizeY) {
                continue;
            }
            LightStorage *target = storage_for(nx, nz);
            if (target == nullptr) {
                // Uninitialized chunk: defer, so init order never matters.
                if (world_.props_at(nx, ny, nz).transparent) {
                    record_offer(nx, ny, nz, channel, node.value);
                }
                continue;
            }
            if (!world_.props_at(nx, ny, nz).transparent) {
                continue;
            }
            const bool down_from_full_sky = channel == Channel::Sky && dir.dy == -1 && node.value == kMaxLight;
            const int next = down_from_full_sky ? node.value : node.value - 1;
            if (next <= 0) {
                continue;
            }
            const auto next_level = static_cast<std::uint8_t>(next);
            const std::uint8_t target_stored =
                channel == Channel::Sky ? target->sky_world(nx, ny, nz) : target->block_light_world(nx, ny, nz);
            if (next_level > target_stored) {
                if (channel == Channel::Sky) {
                    target->set_sky_world(nx, ny, nz, next_level);
                } else {
                    target->set_block_light_world(nx, ny, nz, next_level);
                }
                queue.push_back({nx, ny, nz, next_level});
            }
        }
    }
}

void LightEngine::darken(Channel channel, int world_x, int world_y, int world_z, std::uint8_t seed_value) {
    LightStorage *seed_storage = storage_for(world_x, world_z);
    if (seed_storage == nullptr || seed_value == 0) {
        return;
    }
    if (channel == Channel::Sky) {
        seed_storage->set_sky_world(world_x, world_y, world_z, 0);
    } else {
        seed_storage->set_block_light_world(world_x, world_y, world_z, 0);
    }
    std::deque<Node> queue;
    queue.push_back({world_x, world_y, world_z, seed_value});
    while (!queue.empty()) {
        const Node node = queue.front();
        queue.pop_front();
        for (const Dir &dir : kDirs) {
            const int nx = node.x + dir.dx;
            const int ny = node.y + dir.dy;
            const int nz = node.z + dir.dz;
            if (ny < 0 || ny >= LightStorage::kSizeY) {
                continue;
            }
            LightStorage *target = storage_for(nx, nz);
            if (target == nullptr) {
                continue; // uninitialized chunks hold no light to strip
            }
            const std::uint8_t stored =
                channel == Channel::Sky ? target->sky_world(nx, ny, nz) : target->block_light_world(nx, ny, nz);
            if (stored == 0) {
                continue;
            }
            // Straight down, direct skylight keeps its 15 while descending, so
            // a blocked column must strip the whole 15-run below the change.
            const bool down_full_sky =
                channel == Channel::Sky && dir.dy == -1 && node.value == kMaxLight && stored == kMaxLight;
            if (stored < node.value || down_full_sky) {
                if (channel == Channel::Block) {
                    // A self-emitting cell keeps its own emission; re-source
                    // from it instead of stripping (e.g. a torch next to the
                    // removed light must survive).
                    const std::uint8_t own = world_.props_at(nx, ny, nz).emission;
                    if (own > 0) {
                        if (own != stored) {
                            target->set_block_light_world(nx, ny, nz, own);
                        }
                        enqueue({nx, ny, nz, own}, channel);
                        continue;
                    }
                }
                if (channel == Channel::Sky) {
                    target->set_sky_world(nx, ny, nz, 0);
                } else {
                    target->set_block_light_world(nx, ny, nz, 0);
                }
                queue.push_back({nx, ny, nz, stored});
            } else {
                // Independent light: this cell did not get its level through
                // the removed path, so it re-seeds propagation.
                enqueue({nx, ny, nz, stored}, channel);
            }
        }
    }
}

void LightEngine::drain_queues() {
    propagate(Channel::Sky);
    propagate(Channel::Block);
}

void LightEngine::init_chunk(int chunk_x, int chunk_z) {
    const int base_x = chunk_base(chunk_x);
    const int base_z = chunk_base(chunk_z);
    LightStorage &storage = storages_.try_emplace(Chunk::chunk_coord(base_x, base_z), chunk_x, chunk_z).first->second;
    storage.clear();

    const auto inited = [&](int nx, int nz) {
        return storages_.find(Chunk::chunk_coord(chunk_base(nx), chunk_base(nz))) != storages_.end();
    };

    // Pass 1: per-column highest opaque block (simple heightmap, recomputed
    // per init; see dev report for the caching trade-off).
    std::array<std::array<int, Chunk::kSizeX>, Chunk::kSizeZ> top_opaque{};
    for (int z = 0; z < Chunk::kSizeZ; ++z) {
        for (int x = 0; x < Chunk::kSizeX; ++x) {
            top_opaque[z][x] = -1;
            for (int y = LightStorage::kSizeY - 1; y >= 0; --y) {
                if (!world_.props_at(base_x + x, y, base_z + z).transparent) {
                    top_opaque[z][x] = y;
                    break;
                }
            }
        }
    }

    // Pass 2: direct skylight for every cell above the column blocker. Only
    // cells in the "shadow band" (y at or below some neighbor column's
    // blocker) need seeding: they are the entry points for flood fill into
    // shadowed columns. Flat open terrain therefore enqueues almost nothing.
    // A border column next to a not-yet-initialized chunk seeds fully, so the
    // light still crosses when that chunk arrives (via offers or pull).
    for (int z = 0; z < Chunk::kSizeZ; ++z) {
        for (int x = 0; x < Chunk::kSizeX; ++x) {
            int band_top = -1;
            if (x > 0) {
                band_top = std::max(band_top, top_opaque[z][x - 1]);
            }
            if (x < Chunk::kSizeX - 1) {
                band_top = std::max(band_top, top_opaque[z][x + 1]);
            }
            if (z > 0) {
                band_top = std::max(band_top, top_opaque[z - 1][x]);
            }
            if (z < Chunk::kSizeZ - 1) {
                band_top = std::max(band_top, top_opaque[z + 1][x]);
            }
            const bool needs_border_seed = (x == 0 && !inited(chunk_x - 1, chunk_z)) ||
                                           (x == Chunk::kSizeX - 1 && !inited(chunk_x + 1, chunk_z)) ||
                                           (z == 0 && !inited(chunk_x, chunk_z - 1)) ||
                                           (z == Chunk::kSizeZ - 1 && !inited(chunk_x, chunk_z + 1));
            if (needs_border_seed) {
                band_top = LightStorage::kSizeY - 1;
            }
            for (int y = LightStorage::kSizeY - 1; y > top_opaque[z][x]; --y) {
                storage.set_sky(x, y, z, kMaxLight);
                if (y <= band_top) {
                    sky_add_.push_back({base_x + x, y, base_z + z, kMaxLight});
                }
            }
        }
    }

    // Emitter scan (the registry currently has no light-bearing blocks; the
    // world interface supplies the emission table).
    for (int y = 0; y < LightStorage::kSizeY; ++y) {
        for (int z = 0; z < Chunk::kSizeZ; ++z) {
            for (int x = 0; x < Chunk::kSizeX; ++x) {
                const BlockLightProps props = world_.props_at(base_x + x, y, base_z + z);
                if (props.emission > 0) {
                    storage.set_block_light(x, y, z, props.emission);
                    block_add_.push_back({base_x + x, y, base_z + z, props.emission});
                }
            }
        }
    }

    // Boundary pull: enqueue already-lit border cells of initialized neighbors
    // so their light flows into this chunk. Light flowing the other way is
    // covered by this chunk's own seeds plus the pending-offer replay below.
    const std::array<std::pair<int, int>, 4> kNeighbors{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    for (const auto &neighbor : kNeighbors) {
        const LightStorage *other =
            storage_for(chunk_base(chunk_x + neighbor.first), chunk_base(chunk_z + neighbor.second));
        if (other == nullptr) {
            continue;
        }
        const int other_base_x = chunk_base(chunk_x + neighbor.first);
        const int other_base_z = chunk_base(chunk_z + neighbor.second);
        for (int y = 0; y < LightStorage::kSizeY; ++y) {
            for (int i = 0; i < Chunk::kSizeX; ++i) {
                int wx = 0;
                int wz = 0;
                if (neighbor.first == 1) {
                    wx = base_x + Chunk::kSizeX; // adjacent plane of the +x chunk
                    wz = other_base_z + i;
                } else if (neighbor.first == -1) {
                    wx = base_x - 1;
                    wz = other_base_z + i;
                } else if (neighbor.second == 1) {
                    wx = other_base_x + i;
                    wz = base_z + Chunk::kSizeZ;
                } else {
                    wx = other_base_x + i;
                    wz = base_z - 1;
                }
                const std::uint8_t sky = other->sky_world(wx, y, wz);
                if (sky > 1) {
                    sky_add_.push_back({wx, y, wz, sky});
                }
                const std::uint8_t block = other->block_light_world(wx, y, wz);
                if (block > 1) {
                    block_add_.push_back({wx, y, wz, block});
                }
            }
        }
    }

    // Replay deferred offers addressed to this chunk. Stale entries (the
    // source changed while we were absent) are dropped by the propagate
    // value check.
    const std::int64_t key = Chunk::chunk_coord(base_x, base_z);
    if (const auto it = pending_.find(key); it != pending_.end()) {
        for (const auto &entry : it->second) {
            const std::uint32_t packed = entry.first;
            const int y = static_cast<int>((packed >> 10) & 0x1FF);
            const int lz = static_cast<int>((packed >> 6) & 0x0F);
            const int lx = static_cast<int>((packed >> 1) & 0x0F);
            const Channel channel = (packed & 1U) != 0U ? Channel::Sky : Channel::Block;
            enqueue({base_x + lx, y, base_z + lz, entry.second}, channel);
        }
        pending_.erase(it);
    }

    drain_queues();
}

void LightEngine::on_block_changed(int world_x, int world_y, int world_z, std::uint16_t old_id, std::uint16_t new_id) {
    if (world_y < 0 || world_y >= LightStorage::kSizeY) {
        return;
    }
    LightStorage *storage = storage_for(world_x, world_z);
    if (storage == nullptr) {
        return; // light not computed for this chunk yet; init_chunk catches up
    }
    const int lx = world_x - storage->chunk_x() * Chunk::kSizeX;
    const int lz = world_z - storage->chunk_z() * Chunk::kSizeZ;
    const BlockLightProps old_props = world_.props_of(old_id);
    const BlockLightProps new_props = world_.props_of(new_id);
    std::uint8_t cur_sky = storage->sky(lx, world_y, lz);
    std::uint8_t cur_block = storage->block_light(lx, world_y, lz);

    // 1) The old block emitted and its emission is gone or reduced.
    if (old_props.emission > 0 && cur_block > 0) {
        darken(Channel::Block, world_x, world_y, world_z, cur_block);
        cur_block = storage->block_light(lx, world_y, lz);
    }

    // 2) The cell started blocking light: strip both channels from it.
    if (!new_props.transparent && old_props.transparent) {
        if (cur_sky > 0) {
            darken(Channel::Sky, world_x, world_y, world_z, cur_sky);
        }
        if (cur_block > 0) {
            darken(Channel::Block, world_x, world_y, world_z, cur_block);
        }
    }

    // 3) The cell opened up: re-light it from whatever the live neighbors hold
    //    (a reopened sun shaft resumes at 15 via the downward rule).
    if (new_props.transparent && !old_props.transparent) {
        for (const Dir &dir : kDirs) {
            const int nx = world_x + dir.dx;
            const int ny = world_y + dir.dy;
            const int nz = world_z + dir.dz;
            if (ny < 0 || ny >= LightStorage::kSizeY) {
                continue;
            }
            LightStorage *neighbor = storage_for(nx, nz);
            if (neighbor == nullptr || !world_.props_at(nx, ny, nz).transparent) {
                continue;
            }
            const std::uint8_t sky = neighbor->sky_world(nx, ny, nz);
            if (sky > 0) {
                sky_add_.push_back({nx, ny, nz, sky});
            }
            const std::uint8_t block = neighbor->block_light_world(nx, ny, nz);
            if (block > 0) {
                block_add_.push_back({nx, ny, nz, block});
            }
        }
    }

    // 4) The new block emits: store its level (kept as max with any external
    //    light still present) and spread.
    if (new_props.emission > 0) {
        cur_block = std::max(cur_block, new_props.emission);
        storage->set_block_light(lx, world_y, lz, cur_block);
        block_add_.push_back({world_x, world_y, world_z, cur_block});
    }

    drain_queues();
}

} // namespace opencraft::voxel
