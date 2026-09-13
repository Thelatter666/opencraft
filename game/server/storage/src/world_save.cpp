#include "opencraft/storage/world_save.hpp"

#include <ctime>
#include <future>
#include <utility>

#include "opencraft/core/log.hpp"

namespace opencraft::storage {

namespace {

// Floor division into the 32x32 region grid (negative chunk coords included).
std::pair<int, int> region_key(int cx, int cz) {
    const int rx = cx >> 5;
    const int rz = cz >> 5;
    return {rx, rz};
}

std::pair<int, int> local_key(int cx, int cz) {
    return {cx & 31, cz & 31};
}

} // namespace

WorldSave::WorldSave(std::filesystem::path saves_root, std::string world_name)
    : world_dir_(std::move(saves_root) / std::move(world_name)), region_dir_(world_dir_ / "region"),
      level_path_(world_dir_ / "level.ocd") {
}

WorldSave::~WorldSave() {
    try {
        flush();
    } catch (const std::exception &error) {
        OC_LOG_ERROR("WorldSave dtor flush failed: {}", error.what());
    }
}

std::optional<LevelData> WorldSave::try_read_level() const {
    std::error_code ignored;
    if (!std::filesystem::exists(level_path_, ignored)) {
        return std::nullopt;
    }
    try {
        return read_level(level_path_);
    } catch (const std::exception &error) {
        OC_LOG_ERROR("level.ocd rejected ({}); starting a fresh world", error.what());
        return std::nullopt;
    }
}

void WorldSave::write_level_now(const LevelData &level) {
    write_level(level_path_, level);
}

RegionFile *WorldSave::region_for(int cx, int cz) {
    const auto key = region_key(cx, cz);
    const auto it = regions_.find(key);
    if (it != regions_.end()) {
        return &it->second;
    }
    const std::filesystem::path path = region_dir_ / RegionFile::file_name(key.first, key.second);
    std::error_code ignored;
    if (std::filesystem::exists(path, ignored)) {
        try {
            return &regions_.emplace(key, RegionFile::open(path)).first->second;
        } catch (const std::exception &error) {
            // Card policy: detect, refuse, keep running (chunks regenerate).
            // Quarantine the bad file so the region can start fresh instead
            // of being write-dead forever; the original stays for forensics.
            OC_LOG_ERROR("region file {} rejected ({}); quarantining as .ocr.bad", path.string(), error.what());
            std::error_code rename_error;
            std::filesystem::remove(path.string() + ".bad", rename_error);
            std::filesystem::rename(path, path.string() + ".bad", rename_error);
            if (rename_error) {
                OC_LOG_ERROR("quarantine rename failed: {}", rename_error.message());
                return nullptr;
            }
            return &regions_.emplace(key, RegionFile()).first->second;
        }
    }
    return &regions_.emplace(key, RegionFile()).first->second;
}

std::optional<std::vector<std::uint8_t>> WorldSave::load_chunk(int cx, int cz) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    RegionFile *region = region_for(cx, cz);
    if (region == nullptr || !region->chunk_exists(local_key(cx, cz).first, local_key(cx, cz).second)) {
        return std::nullopt;
    }
    try {
        return region->read_chunk(local_key(cx, cz).first, local_key(cx, cz).second);
    } catch (const std::exception &error) {
        OC_LOG_ERROR("chunk ({}, {}) unreadable ({}); regenerating", cx, cz, error.what());
        return std::nullopt;
    }
}

void WorldSave::mark_dirty(int cx, int cz) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    dirty_.insert({cx, cz});
}

std::vector<std::pair<int, int>> WorldSave::take_dirty() {
    std::lock_guard<std::mutex> lock(io_mutex_);
    std::vector<std::pair<int, int>> out(dirty_.begin(), dirty_.end());
    dirty_.clear();
    return out;
}

std::size_t WorldSave::pending_dirty_count() const {
    std::lock_guard<std::mutex> lock(io_mutex_);
    return dirty_.size();
}

bool WorldSave::is_dirty(int cx, int cz) const {
    std::lock_guard<std::mutex> lock(io_mutex_);
    return dirty_.contains({cx, cz});
}

void WorldSave::store_chunk_sync(int cx, int cz, const std::uint8_t *data, std::size_t size) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    dirty_.erase({cx, cz});
    RegionFile *region = region_for(cx, cz);
    if (region == nullptr) {
        return;
    }
    const auto [lx, lz] = local_key(cx, cz);
    region->write_chunk(lx, lz, data, size, static_cast<std::uint32_t>(::time(nullptr)));
    try {
        region->save(region_dir_ / RegionFile::file_name(region_key(cx, cz).first, region_key(cx, cz).second));
    } catch (const std::exception &error) {
        OC_LOG_ERROR("region snapshot failed for chunk ({}, {}): {}", cx, cz, error.what());
    }
}

void WorldSave::store_chunk_async(int cx, int cz, std::vector<std::uint8_t> payload) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    dirty_.erase({cx, cz});
    futures_.push_back(io_pool_.submit(
        [this, cx, cz, payload = std::move(payload)] { store_chunk_sync(cx, cz, payload.data(), payload.size()); }));
}

bool WorldSave::maybe_autosave_tick() {
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (++autosave_counter_ < kAutosaveTicks) {
        return false;
    }
    autosave_counter_ = 0;
    return true;
}

void WorldSave::flush() {
    // Wait for the queued writer tasks, then hand the pool a final no-op so
    // "flushed" means "every store submitted before flush() completed".
    std::vector<std::future<void>> pending;
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        pending.swap(futures_);
    }
    for (std::future<void> &future : pending) {
        try {
            future.get();
        } catch (const std::exception &error) {
            OC_LOG_ERROR("background chunk write failed: {}", error.what());
        }
    }
}

std::size_t WorldSave::cached_region_count() const {
    std::lock_guard<std::mutex> lock(io_mutex_);
    return regions_.size();
}

} // namespace opencraft::storage
