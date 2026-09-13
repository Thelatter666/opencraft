#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opencraft::voxel {

// Transparent string hash enabling string_view lookups without a copy.
struct StringHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view value) const { return std::hash<std::string_view>{}(value); }
};

// Static description of a block type. `solid` and `transparent` are
// intentionally separate flags: water is non-solid yet non-transparent,
// and merging them into one boolean breaks lighting/meshing later on.
struct BlockDef {
    std::string display_name;
    bool solid = true;        // participates in entity collision
    bool transparent = false; // does not block light / is meshed as see-through
    float hardness = 0.0f;    // seconds-scale break hardness; < 0 means unbreakable
};

// String-id to runtime numeric-id mapping (docs/03 §7). Numeric ids are
// dense u16 values starting at 0; id 0 is reserved for air.
class BlockRegistry {
public:
    static constexpr std::uint16_t kAirId = 0;

    // A registry pre-seeded with air plus the ~20 launch blocks.
    [[nodiscard]] static BlockRegistry create_default();

    BlockRegistry();

    // Registers a new block and returns its numeric id. Throws
    // std::invalid_argument on an empty id or a duplicate registration.
    std::uint16_t register_block(std::string id, BlockDef def);

    [[nodiscard]] bool has_id(std::string_view id) const;
    [[nodiscard]] bool has_numeric(std::uint16_t numeric_id) const;

    // id -> numeric id. nullopt for unknown ids.
    [[nodiscard]] std::optional<std::uint16_t> find_id(std::string_view id) const;
    // Throwing variant; std::out_of_range for unknown ids.
    [[nodiscard]] std::uint16_t id_of(std::string_view id) const;

    // numeric id -> string id. Throwing variant; std::out_of_range for
    // unknown numeric ids.
    [[nodiscard]] const std::string &string_of(std::uint16_t numeric_id) const;

    [[nodiscard]] const BlockDef &def_of(std::uint16_t numeric_id) const;

    [[nodiscard]] std::uint16_t air() const { return kAirId; }

    [[nodiscard]] std::size_t size() const { return defs_.size(); }

private:
    std::unordered_map<std::string, std::uint16_t, StringHash, std::equal_to<>> numeric_by_id_;
    std::vector<std::string> id_by_numeric_;
    std::vector<BlockDef> defs_;
};

} // namespace opencraft::voxel
