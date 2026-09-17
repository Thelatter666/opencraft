#pragma once

// T-M2: the mob spawner - the rules of docs/01 §6 and research/01 §10.3 on top of
// the entity store. Like item_sim/mob_sim it is a set of free functions over
// (store, world, registries, rules): the caller owns the state and decides when a
// step runs.
//
// ⚖ The rules, each with its source:
//   * HOSTILE mobs spawn where the effective light level is 0 (docs/01 §6
//     "敌对生成光照等级 0"). ⚠ research/01 §10.3 phrases it as "光照 ≤ 随机 0–7
//     (内天空光 ≤7 且方块光 0)" - the spec is the stricter number and the spec
//     wins here; the discrepancy is in the report.
//   * They spawn in a 24-128 block ring around the player and are removed beyond
//     128 (docs/01 §6 "玩家 24–128 格环带内可刷；>128 格即时消除").
//     ⚠ research/01 §10.3 adds a separate "32–128 格随机消失 (1/800 per tick)"
//     rule; it is NOT implemented (it needs a per-tick random draw and the card's
//     acceptance only names the 128 removal) and is listed as a gap.
//   * MOB CAP: 敌对 70 × 可刷怪区块数 / 289 (docs/01 §6), 被动生物 (creature) 10 ×
//     the same fraction (research/01 §10.3's creature row). The 289 is the 17x17
//     chunk window that count is defined over.
//   * PASSIVE mobs are generated ONCE per chunk (docs/01 §6 "被动生物按区块一次性
//     生成"), which is why the spawner remembers the chunks it has already
//     populated instead of re-rolling every tick.
//   * PACKS: research/01 §10.3 - a pack centre is picked per chunk and up to 4
//     members spawn within ±5 blocks of it (§10.2: 僵尸 4 只成组).
//
// ── THE AFK/DARKNESS PROBLEM THIS WORLD HAS ────────────────────────────────
// The hostile light rule is "effective light 0", and this world has no day/night
// clock and no light-emitting block yet, so the only light-0 space is under a
// solid roof (skylight stops at the roof). Hostile mobs therefore appear in
// caves and under overhangs, and NOT on open grass - which is the rule working as
// written, but it also means the live evidence for the hostile half of the roster
// needs a roofed area or a cave. Recorded in the report.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "opencraft/game/mob_type.hpp"
#include "opencraft/sim/entity_store.hpp"
#include "opencraft/sim/mob_sim.hpp"
#include "opencraft/voxel/chunk.hpp"

namespace opencraft::server {

// Every spawn number, in one place, each with its source.
struct MobSpawnRules {
    // ⚖ docs/01 §6.
    double spawn_ring_min = 24.0;
    double spawn_ring_max = 128.0;
    double despawn_range = 128.0;

    // ⚖ docs/01 §6: 敌对 mob cap = 70 × 可刷区块 / 289.
    double hostile_cap_per_window = 70.0;
    // ⚖ research/01 §10.3: 生物（creature）cap 10 (same denominator).
    double passive_cap_per_window = 10.0;
    int cap_window_chunks = 289; // 17 x 17

    // ⚖ research/01 §10.3: 敌对每 tick 尝试. The passive side has no cadence here
    // on purpose: docs/01 §6's rule is "once per chunk", which the ledger below
    // expresses exactly, and a second timer on top of it would just add a way for
    // the two to disagree.
    int hostile_attempt_period = 1;

    // ⚠ 待校准: how far from the spawn cell the spawner looks for a floor with
    // headroom. The source describes "random point in the chunk, moved to the
    // nearest air cell" without a distance; 4 blocks down / 2 up is this card's
    // probe.
    int floor_probe_down = 4;
    int floor_probe_up = 2;

    // ⚖ research/01 §10.3: 包中心随机、成员 ±5 格三角形分布偏移, 包大小一般 4.
    int pack_size = 4;
    double pack_spread = 5.0;

    // ⚠ 待校准: how many spawn attempts one call may make. Attempts are cheap but
    // they are not free (a light query and a small column scan each), and a capped
    // number keeps the per-tick cost bounded and the behaviour reproducible.
    int attempts_per_call = 4;
};

// Which chunks have already had their one-shot passive population (docs/01 §6
// 被动生物按区块一次性生成). Small and explicit: it is the only piece of spawner
// state that has to survive between calls, and keeping it out of the entity store
// is what makes it obvious that it is spawner bookkeeping, not world state.
class PassiveChunkLedger {
public:
    [[nodiscard]] bool seen(int cx, int cz) const { return seen_.contains(key(cx, cz)); }

    void mark(int cx, int cz) { seen_.insert(key(cx, cz)); }

private:
    [[nodiscard]] static std::uint64_t key(int cx, int cz) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) | static_cast<std::uint32_t>(cz);
    }

    std::unordered_set<std::uint64_t> seen_;
};

// How many mobs of one class are alive, and how many of them are near `centre`
// (the despawn test needs the distance, the cap needs the population).
struct MobPopulation {
    std::size_t hostile = 0;
    std::size_t passive = 0;
};

[[nodiscard]] inline MobPopulation count_mobs(const EntityStore &store, const game::MobRegistry &mobs) {
    MobPopulation population;
    store.for_each_entity([&population, &mobs](const Entity &entity) {
        const game::MobDef *def = mobs.find(entity.type);
        if (def == nullptr) {
            return;
        }
        if (def->mob_class == game::MobClass::Hostile) {
            ++population.hostile;
        } else {
            ++population.passive;
        }
    });
    return population;
}

namespace spawn_detail {

// A tiny deterministic RNG keyed by (seed, chunk) so a chunk's population is a
// pure function of the world seed and its coordinates - reloading a world
// produces the same mobs, and a test can assert an exact outcome.
[[nodiscard]] inline std::uint64_t chunk_seed(const std::uint64_t world_seed, const int cx, const int cz) {
    std::uint64_t state = world_seed ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) ^
                          static_cast<std::uint32_t>(cz) ^ 0x5DEECE66DULL;
    return mob_detail::next_random(state);
}

// A random column of the chunk.
[[nodiscard]] inline std::pair<int, int> random_column(std::uint64_t &rng, const int cx, const int cz) {
    return {cx * voxel::Chunk::kSizeX +
                static_cast<int>(mob_detail::random_range(rng, 0.0, static_cast<double>(voxel::Chunk::kSizeX))),
            cz * voxel::Chunk::kSizeZ +
                static_cast<int>(mob_detail::random_range(rng, 0.0, static_cast<double>(voxel::Chunk::kSizeZ)))};
}

// Is (wx, y+1, wz) a legal standing cell: solid floor, two air cells, no liquid?
[[nodiscard]] inline bool standable(const IMobWorld &world, const MobSpawnRules &rules, const int wx, const int wy,
                                    const int wz) {
    (void) rules;
    return world.solid_at(wx, wy, wz) && !world.solid_at(wx, wy + 1, wz) && !world.solid_at(wx, wy + 2, wz) &&
           !world.liquid_at(wx, wy + 1, wz);
}

// The SURFACE probe: the topmost solid cell of a random column of the chunk, if it
// has room for a body on top of it.
//
// ⚠ It deliberately does NOT keep descending. An earlier version continued past an
// ocean floor and returned a cell inside a CAVE several dozen blocks underground -
// which the light rule then happily accepted, so the world filled up with mobs
// nobody could ever reach. "The topmost solid cell of the column" is the surface;
// anything below it is the cave probe's business, not this one's.
[[nodiscard]] inline bool find_surface_spot(const IMobWorld &world, const MobSpawnRules &rules, std::uint64_t &rng,
                                            const int cx, const int cz, glm::dvec3 &out) {
    const auto [wx, wz] = random_column(rng, cx, cz);
    const int min_y = rules.floor_probe_down;
    for (int y = voxel::Chunk::kSizeY - 2; y > min_y; --y) {
        const bool here = world.solid_at(wx, y, wz);
        const bool above = world.solid_at(wx, y + 1, wz);
        if (!here && !above) {
            continue; // still in the open air above the terrain
        }
        if (!here) {
            return false; // the column's first obstruction is not a floor (leaves, a ceiling)
        }
        if (!standable(world, rules, wx, y, wz)) {
            return false; // a floor with no room on it, or a water surface
        }
        out = glm::dvec3(static_cast<double>(wx) + 0.5, static_cast<double>(y) + 1.0, static_cast<double>(wz) + 0.5);
        return true;
    }
    return false;
}

// The CAVE probe: a random height BELOW the surface, with the same floor/headroom
// rule. This is where hostiles actually live - ⚖ docs/01 §6's light level 0 is
// "under a roof", and research/01 §10.3's own phrasing is "区块内随机点" rather than
// "on the surface". The surface probe is tried first, so a mob under a natural
// overhang is still found that way.
//
// `surface_y` bounds the search from above: probing above the surface would just
// re-find the surface.
[[nodiscard]] inline bool find_cave_spot(const IMobWorld &world, const MobSpawnRules &rules, std::uint64_t &rng,
                                         const int cx, const int cz, const int surface_y, glm::dvec3 &out) {
    if (surface_y <= rules.floor_probe_down + 2) {
        return false; // no underground to search in this column
    }
    const auto [wx, wz] = random_column(rng, cx, cz);
    const int highest = surface_y - 2;
    const int y =
        rules.floor_probe_down +
        static_cast<int>(mob_detail::random_range(rng, 0.0, static_cast<double>(highest - rules.floor_probe_down)));
    if (!standable(world, rules, wx, y, wz)) {
        return false;
    }
    out = glm::dvec3(static_cast<double>(wx) + 0.5, static_cast<double>(y) + 1.0, static_cast<double>(wz) + 0.5);
    return true;
}

// The topmost solid cell of a column (the surface height), or -1. Used to bound the
// cave probe. Cheap: one walk of a column, and only for the columns already chosen.
[[nodiscard]] inline int column_surface_y(const IMobWorld &world, const int wx, const int wz) {
    for (int y = voxel::Chunk::kSizeY - 2; y > 0; --y) {
        if (world.solid_at(wx, y, wz)) {
            return y;
        }
    }
    return -1;
}

// Is this position a legal spawn for `def`?
[[nodiscard]] inline bool position_allows(const IMobWorld &world, const game::MobDef &def, const glm::dvec3 &spot) {
    if (def.mob_class == game::MobClass::Hostile) {
        // ⚖ docs/01 §6: 敌对生成光照等级 0. The light of the cell the BODY will
        // occupy, which is what the base game checks.
        const int wx = static_cast<int>(std::floor(spot.x));
        const int wy = static_cast<int>(std::floor(spot.y));
        const int wz = static_cast<int>(std::floor(spot.z));
        if (world.light_at(wx, wy, wz) != 0) {
            return false;
        }
    }
    // Everything needs a solid floor and a body's worth of headroom (re-checked
    // here because a pack member is offset from the probed spot).
    const int wx = static_cast<int>(std::floor(spot.x));
    const int wy = static_cast<int>(std::floor(spot.y));
    const int wz = static_cast<int>(std::floor(spot.z));
    return world.solid_at(wx, wy - 1, wz) && !world.solid_at(wx, wy, wz) && !world.solid_at(wx, wy + 1, wz) &&
           !world.liquid_at(wx, wy, wz);
}

// ⚖ The 24-128 ring, measured from the player. `min`/`max` apply to the
// horizontal distance in the base game's own phrasing ("球形半径"), with the
// vertical extent folded in - a mob directly below the player at 100 blocks is
// still 100 blocks away.
[[nodiscard]] inline bool in_ring(const glm::dvec3 &spot, const glm::dvec3 &player, const double min,
                                  const double max) {
    const double distance = glm::length(spot - player);
    return distance >= min && distance <= max;
}

} // namespace spawn_detail

// The result of one spawn pass, for logging and for the tests.
struct MobSpawnResult {
    std::vector<EntityId> spawned;
    std::vector<EntityId> despawned;
    std::size_t hostile_population = 0;
    std::size_t passive_population = 0;
    int hostile_cap = 0;
    int passive_cap = 0;
    // The window the caps were divided over - the number of chunks currently
    // loaded, which is what "可刷区块数" means for a game that streams a handful of
    // chunks rather than all 289 of a 17x17 view.
    int loaded_chunks = 0;
    bool hostile_cap_reached = false;
};

// One spawn pass for a single chunk. Called by the authority's spawn step for
// chunks near the player; `ledger` is the per-chunk passive bookkeeping.
//
// The hostile half runs on every call (capped by the mob cap and the light rule,
// which is what makes it do nothing at all in daylight); the passive half runs
// once per chunk and is then remembered.
[[nodiscard]] inline MobSpawnResult spawn_in_chunk(EntityStore &store, const IMobWorld &world,
                                                   const game::MobRegistry &mobs, const MobSpawnRules &rules,
                                                   PassiveChunkLedger &ledger, const std::uint64_t world_seed,
                                                   const glm::dvec3 &player, const game::Difficulty difficulty,
                                                   const int cx, const int cz, const std::uint64_t tick,
                                                   const int loaded_chunks) {
    MobSpawnResult result;
    if (!world.chunk_loaded(cx, cz)) {
        return result;
    }
    const std::vector<std::uint16_t> hostiles = mobs.entity_types(game::MobClass::Hostile);
    const std::vector<std::uint16_t> passives = mobs.entity_types(game::MobClass::Passive);
    const MobPopulation population = count_mobs(store, mobs);
    result.hostile_population = population.hostile;
    result.passive_population = population.passive;

    // ⚖ The cap is a fraction of the chunks in the mob-ticking window (289 = the
    // 17x17 view the number is defined over). This game streams far fewer chunks,
    // so the count the caller passes is what keeps the cap proportional instead of
    // letting a 6-chunk world hold 70 hostile mobs.
    result.loaded_chunks = loaded_chunks;
    const double window = static_cast<double>(rules.cap_window_chunks);
    result.hostile_cap = static_cast<int>(rules.hostile_cap_per_window * static_cast<double>(loaded_chunks) / window);
    result.passive_cap = static_cast<int>(rules.passive_cap_per_window * static_cast<double>(loaded_chunks) / window);

    // ⚖ The hostile roster does not exist in a peaceful world (docs/01 §7).
    const bool hostiles_allowed = difficulty != game::Difficulty::Peaceful && !hostiles.empty();

    // The stream is keyed by (seed, chunk, TICK). The tick is in there in full and
    // not coarsened: an earlier version folded it with `tick / 64` (to keep a
    // chunk's population stable within a second or so), and the on-machine log
    // then showed four hostile mobs spawned in four consecutive ticks standing in
    // the SAME cell - the same draws, because within one 64-tick window the seed
    // was the same and the pack offsets were therefore re-applied to the same
    // spot. Determinism is preserved either way (same inputs, same result); what
    // the full tick buys is that a pack spread actually spreads.
    std::uint64_t rng = spawn_detail::chunk_seed(world_seed, cx, cz) ^ tick;

    // ── hostiles: light 0, inside the ring, under the cap, in packs ─────────
    // Surface first (a mob under a natural overhang IS a light-0 surface spawn),
    // then underground, which is where the light rule puts most of them.
    const bool hostile_due =
        rules.hostile_attempt_period > 0 && (tick % static_cast<std::uint64_t>(rules.hostile_attempt_period) == 0);
    std::size_t alive = population.hostile;
    glm::dvec3 centre;
    bool have_centre = false;
    if (hostiles_allowed && hostile_due && static_cast<int>(alive) < result.hostile_cap) {
        const std::uint16_t type = hostiles[static_cast<std::size_t>(
            mob_detail::random_range(rng, 0.0, static_cast<double>(hostiles.size()) - 1e-9))];
        const game::MobDef *def = mobs.find(type);
        if (def != nullptr && spawn_detail::find_surface_spot(world, rules, rng, cx, cz, centre) &&
            spawn_detail::position_allows(world, *def, centre)) {
            have_centre = true;
        } else if (def != nullptr) {
            const auto [probe_x, probe_z] = spawn_detail::random_column(rng, cx, cz);
            const int surface_y = spawn_detail::column_surface_y(world, probe_x, probe_z);
            have_centre = spawn_detail::find_cave_spot(world, rules, rng, cx, cz, surface_y, centre) &&
                          spawn_detail::position_allows(world, *def, centre);
        }
    }
    if (have_centre) {
        const std::uint16_t type = hostiles[static_cast<std::size_t>(
            mob_detail::random_range(rng, 0.0, static_cast<double>(hostiles.size()) - 1e-9))];
        const game::MobDef *def = mobs.find(type);
        // ⚖ 成员 ±5 格三角形分布偏移: the offset shrinks with the member index, which is
        // the "triangle" the source describes.
        for (int member = 0; member < rules.pack_size && static_cast<int>(alive) < result.hostile_cap; ++member) {
            const double spread =
                rules.pack_spread * (1.0 - static_cast<double>(member) / static_cast<double>(rules.pack_size));
            const glm::dvec3 spot = centre + glm::dvec3(mob_detail::random_range(rng, -spread, spread), 0.0,
                                                        mob_detail::random_range(rng, -spread, spread));
            if (!spawn_detail::in_ring(spot, player, rules.spawn_ring_min, rules.spawn_ring_max)) {
                continue;
            }
            if (!spawn_detail::position_allows(world, *def, spot)) {
                continue;
            }
            const EntityId id = spawn_mob(store, mobs, type, spot, world_seed, MobRules{});
            if (id != EntityStore::kNoEntity) {
                result.spawned.push_back(id);
                ++alive;
            }
        }
    }
    result.hostile_cap_reached = static_cast<int>(alive) >= result.hostile_cap;

    // ── passives: once per chunk, on the surface ───────────────────────────
    // ⚖ docs/01 §6: 被动生物按区块一次性生成.
    if (!ledger.seen(cx, cz)) {
        ledger.mark(cx, cz);
        if (!passives.empty() && static_cast<int>(population.passive) < result.passive_cap &&
            spawn_detail::find_surface_spot(world, rules, rng, cx, cz, centre)) {
            const std::uint16_t type = passives[static_cast<std::size_t>(
                mob_detail::random_range(rng, 0.0, static_cast<double>(passives.size()) - 1e-9))];
            const game::MobDef *def = mobs.find(type);
            std::size_t alive_passive = population.passive;
            if (def != nullptr && spawn_detail::position_allows(world, *def, centre)) {
                for (int member = 0; member < rules.pack_size && static_cast<int>(alive_passive) < result.passive_cap;
                     ++member) {
                    const double spread =
                        rules.pack_spread * (1.0 - static_cast<double>(member) / static_cast<double>(rules.pack_size));
                    const glm::dvec3 spot = centre + glm::dvec3(mob_detail::random_range(rng, -spread, spread), 0.0,
                                                                mob_detail::random_range(rng, -spread, spread));
                    if (!spawn_detail::in_ring(spot, player, rules.spawn_ring_min, rules.spawn_ring_max)) {
                        continue;
                    }
                    if (!spawn_detail::position_allows(world, *def, spot)) {
                        continue;
                    }
                    const EntityId id = spawn_mob(store, mobs, type, spot, world_seed, MobRules{});
                    if (id != EntityStore::kNoEntity) {
                        result.spawned.push_back(id);
                        ++alive_passive;
                    }
                }
            }
        }
    }
    return result;
}

// ⚖ docs/01 §6: 玩家 >128 格即时消除. Called once per tick with the player's
// position. It needs no world query at all - the rule is pure distance - which is
// also why a mob frozen in an unloaded chunk is still removed by it.
[[nodiscard]] inline std::size_t despawn_distant_mobs(EntityStore &store, const game::MobRegistry &mobs,
                                                      const MobSpawnRules &rules, const glm::dvec3 &player) {
    std::size_t removed = 0;
    for (const EntityId id : store.live_ids()) {
        const Entity *entity = store.find(id);
        if (entity == nullptr || mobs.find(entity->type) == nullptr) {
            continue;
        }
        if (glm::length(entity->position - player) > rules.despawn_range) {
            store.erase(id);
            ++removed;
        }
    }
    return removed;
}

} // namespace opencraft::server
