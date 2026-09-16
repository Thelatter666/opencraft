#include "opencraft/game/entity_type.hpp"

#include <stdexcept>
#include <utility>

namespace opencraft::game {

namespace {

// ── Launch entity set ─────────────────────────────────────────────────────
// One entry, because one entity kind exists (the card scopes mobs OUT: 不做生物
// AI / 生物实体). The table is here anyway - rather than a hard-coded enum -
// because the registry, not the code, is what a later card extends.
constexpr struct {
    const char *id;
    const char *display_name;
    double half_width;
    double height;
    double gravity;
    double vertical_drag;
    double horizontal_drag;
    int max_health;
    bool carries_item_stack;
} kLaunchTypes[] = {
    // The dropped item (research/11 §4.1-4.4):
    //   0.25^3 box, gravity -0.04, drag 0.98/0.98, 5 HP, item payload.
    // Terminal velocity is NOT stored: it is derived by the parameters above
    // (gravity x vertical_drag / (1 - vertical_drag) = 1.96 blocks/tick =
    // 39.2 m/s, research/11 §4.1) and asserted in the tests, so a change to
    // either parameter cannot leave a stale third constant behind.
    {"item", "Item Drop", 0.125, 0.25, 0.04, 0.98, 0.98, 5, true},
};

} // namespace

EntityTypeRegistry::EntityTypeRegistry() {
    EntityDef empty_def;
    empty_def.display_name = "Empty";
    defs_.push_back(std::move(empty_def));
    id_by_numeric_.emplace_back("empty");
    numeric_by_id_.emplace("empty", kEmptyId);
}

EntityTypeRegistry EntityTypeRegistry::create_default() {
    EntityTypeRegistry registry;
    for (const auto &entry : kLaunchTypes) {
        registry.register_type(entry.id,
                               {entry.display_name, entry.half_width, entry.height, entry.gravity, entry.vertical_drag,
                                entry.horizontal_drag, entry.max_health, entry.carries_item_stack});
    }
    return registry;
}

std::uint16_t EntityTypeRegistry::register_type(std::string id, EntityDef def) {
    if (id.empty()) {
        throw std::invalid_argument("entity type id must not be empty");
    }
    if (numeric_by_id_.contains(id)) {
        throw std::invalid_argument("entity type id already registered: " + id);
    }
    if (!(def.half_width > 0.0) || !(def.height > 0.0)) {
        throw std::invalid_argument("entity type must have a positive collision box: " + id);
    }
    if (defs_.size() > 0xFFFF) {
        throw std::overflow_error("entity type registry exhausted (u16 id space)");
    }
    const auto numeric_id = static_cast<std::uint16_t>(defs_.size());
    defs_.push_back(std::move(def));
    id_by_numeric_.push_back(id);
    numeric_by_id_.emplace(id, numeric_id);
    return numeric_id;
}

bool EntityTypeRegistry::has_id(std::string_view id) const {
    return numeric_by_id_.contains(id);
}

bool EntityTypeRegistry::has_numeric(std::uint16_t numeric_id) const {
    return numeric_id < defs_.size();
}

std::optional<std::uint16_t> EntityTypeRegistry::find_id(std::string_view id) const {
    const auto it = numeric_by_id_.find(id);
    if (it == numeric_by_id_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::uint16_t EntityTypeRegistry::id_of(std::string_view id) const {
    const auto found = find_id(id);
    if (!found) {
        throw std::out_of_range("unknown entity type id: " + std::string(id));
    }
    return *found;
}

const std::string &EntityTypeRegistry::string_of(std::uint16_t numeric_id) const {
    if (numeric_id >= id_by_numeric_.size()) {
        throw std::out_of_range("unknown entity type numeric id");
    }
    return id_by_numeric_[numeric_id];
}

const EntityDef &EntityTypeRegistry::def_of(std::uint16_t numeric_id) const {
    if (numeric_id >= defs_.size()) {
        throw std::out_of_range("unknown entity type numeric id");
    }
    return defs_[numeric_id];
}

} // namespace opencraft::game
