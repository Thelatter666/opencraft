#pragma once

// One 20 TPS logic tick (input mapping -> physics -> targeting -> mining ->
// placement -> fluid -> autosave) plus the session helpers main.cpp shares with
// it. Moved verbatim out of main.cpp by T-M1 (pure code motion): the statement
// order, every branch and every ⚖ number are unchanged. The context carries
// references to main()'s frame-loop locals, so nothing is copied and writes
// land in the same objects the pre-split lambda captured.

#include <cstdint>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "interaction.hpp"
#include "opencraft/game/mining.hpp"
#include "opencraft/physics/player_state.hpp"
#include "opencraft/storage/level_file.hpp"
#include "opencraft/storage/world_save.hpp"
#include "particles.hpp"

// Forward declaration: the tick only holds the handle, so no GLFW include.
struct GLFWwindow;

namespace opencraft::client {

class WorldSource;

// Every member aliases a main() local; build it once before the frame loop.
struct TickContext {
    WorldSource &world;
    storage::WorldSave &save;
    const std::vector<glm::vec3> &block_colors;
    GLFWwindow *window;
    const std::uint64_t &world_seed;
    const glm::dvec3 &spawn_pos;
    std::uint64_t &game_ticks;
    double &view_yaw;
    double &view_pitch;
    bool &auto_jump_enabled;
    physics::PlayerState &prev_state;
    physics::PlayerState &curr_state;
    game::MiningTracker &mining;
    std::vector<std::pair<int, int>> &dirty_chunks;
    std::vector<Particle> &particles;
    InteractionState &state;
};

// GLFW_KEY_* poll; main.cpp drives the menu keys through this too.
[[nodiscard]] bool key_pressed(GLFWwindow *window, int key);

// View direction from yaw/pitch (MC convention: -Z forward at yaw 0).
[[nodiscard]] glm::dvec3 view_dir(double view_yaw, double view_pitch);

// Level snapshot for periodic + exit saves (T009). Field-for-field the inverse
// of storage::serialize_level - keep both in sync.
[[nodiscard]] storage::LevelData make_level_data(const TickContext &ctx);

// One 20 TPS logic tick: physics -> targeting -> mining -> placement.
void run_tick(const TickContext &ctx);

} // namespace opencraft::client
