#pragma once

#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "opencraft/core/task_system.hpp"
#include "opencraft/storage/level_file.hpp"
#include "opencraft/storage/region_file.hpp"

namespace opencraft::storage {

// Facade over one saved world directory (T009):
//
//   <saves>/<world>/level.ocd              - global + player state (level_file.hpp)
//   <saves>/<world>/region/r.<rx>.<rz>.ocr - 32x32-chunk region files
//
// The client feeds back (a) which chunks changed (mark_dirty) and (b) the
// serialized bytes of a chunk at save time (store_chunk_async). The owner's
// autosave pass (maybe_autosave_tick, default cadence 10 s) drains the dirty
// set on the main thread, serializes the chunks, and hands the bytes to a
// dedicated single-worker task pool (the "low-priority IO thread" the card
// asks for, built on the existing task_system). The writer applies payloads
// to the in-memory RegionFiles and snapshots every touched region file
// atomically (temp + fsync + rename).
//
// Locking: one io_mutex_ guards the region cache and dirty sets. Snapshot
// writes run under it; a concurrent load_chunk on the main thread may briefly
// wait behind a snapshot. Acceptable at M1 scale (few small files, 10 s
// cadence).
//
// Corruption policy: a region file that fails to open is reported and treated
// as absent - its chunks regenerate from the seed; the game never crashes on
// bad saves (card: 损坏文件能被检测并拒绝加载，报错不崩溃).
class WorldSave {
public:
    // Default autosave cadence: 200 calls = 200 logic ticks at 20 TPS = 10 s.
    static constexpr std::uint64_t kAutosaveTicks = 200;

    WorldSave(std::filesystem::path saves_root, std::string world_name = "world");
    ~WorldSave();

    WorldSave(const WorldSave &) = delete;
    WorldSave &operator=(const WorldSave &) = delete;

    [[nodiscard]] const std::filesystem::path &world_dir() const { return world_dir_; }
    [[nodiscard]] const std::filesystem::path &level_path() const { return level_path_; }

    // --- level -----------------------------------------------------------------
    // Maps "missing/corrupt" to nullopt (report-and-continue policy above).
    [[nodiscard]] std::optional<LevelData> try_read_level() const;
    void write_level_now(const LevelData &level);

    // --- chunks ----------------------------------------------------------------
    // Loads the uncompressed chunk payload; nullopt when never saved (or the
    // backing region file is corrupt - policy above).
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> load_chunk(int cx, int cz);

    // Marks a loaded-and-modified chunk dirty; the owner serializes it during
    // the next autosave pass (take_dirty + store_chunk_async) or at flush.
    void mark_dirty(int cx, int cz);

    [[nodiscard]] std::vector<std::pair<int, int>> take_dirty();

    // True when the chunk has unsaved modifications (set since the last
    // store/flush of that chunk).
    [[nodiscard]] bool is_dirty(int cx, int cz) const;

    [[nodiscard]] std::size_t pending_dirty_count() const;

    // Queues one chunk's serialized payload for the async writer; also clears
    // the dirty flag for that chunk.
    void store_chunk_async(int cx, int cz, std::vector<std::uint8_t> payload);

    // Synchronous variant for flush-before-exit paths and tests.
    void store_chunk_sync(int cx, int cz, const std::uint8_t *data, std::size_t size);

    // Call once per logic tick; returns true once per cadence window.
    [[nodiscard]] bool maybe_autosave_tick();

    // Snapshots every touched region file and waits for the worker. Idempotent.
    void flush();

    // Testing seam: how many region files are cached in memory.
    [[nodiscard]] std::size_t cached_region_count() const;

private:
    // Cached-or-opened region file for (cx, cz); nullptr when the backing
    // file is corrupt (policy above). rx = cx >> 5 (floor), etc.
    RegionFile *region_for(int cx, int cz);

    std::filesystem::path world_dir_;
    std::filesystem::path region_dir_;
    std::filesystem::path level_path_;

    mutable std::mutex io_mutex_;
    std::set<std::pair<int, int>> dirty_;
    std::map<std::pair<int, int>, RegionFile> regions_;
    std::vector<std::future<void>> futures_; // outstanding async chunk writes
    std::uint64_t autosave_counter_ = 0;

    core::TaskSystem io_pool_{1};
};

} // namespace opencraft::storage
