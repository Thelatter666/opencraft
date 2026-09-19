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

#include "crafting_ui.hpp"
#include "interaction.hpp"
#include "opencraft/game/mining.hpp"
#include "opencraft/game/protocol.hpp"
#include "opencraft/physics/player_state.hpp"
#include "opencraft/storage/level_file.hpp"
#include "opencraft/storage/world_save.hpp"
#include "particles.hpp"
#include "player_life.hpp"

// Forward declaration: the tick only holds the handle, so no GLFW include.
struct GLFWwindow;

namespace opencraft::client {

class WorldSource;

// Every member aliases a main() local; build it once before the frame loop.
struct TickContext {
    // T-A1: the world arrives in two pieces. `world` is the read-only view the
    // targeting code queries; `authority` is the only thing that can change it
    // (submit() per action, tick() for the fluid step, take_changes() for the
    // stale-chunk push-back the client remeshes from).
    WorldSource &world;
    game::IAuthority &authority;
    storage::WorldSave &save;
    const std::vector<glm::vec3> &block_colors;
    GLFWwindow *window;
    const std::uint64_t &world_seed;
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
    // T-D45: the player's life cycle - alive/dead, where the corpse is, and the
    // respawn point - plus the one gamerule the death path reads.
    PlayerLife &life;
    const GameRules &rules;
    // ── T-D60: the crafting screen (C-3's UI-open flag) ─────────────────────
    // The tick needs the screen for exactly two things: to open the bench's 3x3
    // surface when one is right-clicked, and to know whether a surface is up at
    // all - which is what `ui_open(ctx)` below answers. Keeping the screen here
    // rather than a separate bool means the flag is DERIVED, so the tick and the
    // UI cannot disagree about whether the player is looking at a screen.
    CraftingState &crafting;
};

// T-D60 C-3: is a client UI owning the pointer this tick? Nothing in the world
// may be touched while this is true - see submit_world_verb below for the funnel
// that enforces it, and run_tick's own early return for the rest.
[[nodiscard]] inline bool ui_open(const TickContext &ctx) {
    return ctx.crafting.open();
}

// T-D60 C-3: the single funnel every world verb of a logic tick goes through.
//
// While a UI owns the pointer the request is NOT built into a submission and not
// "sent and refused": it is dropped here, so no code path downstream can change
// its mind. The returned ActionResult is a plain refusal with no reason, which is
// what the callers already render for "the action did not happen".
//
// It is a free function (and not a member of the context) so a test can drive it
// against a counting stub authority and assert that nothing was submitted - the
// card's "界面打开时 Dig/PlaceBlock/Attack 全拒" as a unit test rather than as a
// reading of run_tick.
//
// Inline (and not a run_tick-local) so the tests can drive it against a counting
// stub authority: the card asks for a unit test that PROVES nothing is
// submitted, and a promise about a function a test cannot call is not that.
[[nodiscard]] inline game::ActionResult submit_world_verb(game::IAuthority &authority, const bool ui_open_flag,
                                                          const game::ActionRequest &request) {
    if (ui_open_flag) {
        // Nothing was asked, so there is no reason to report one: the callers
        // read `accepted` and only log a reason on the paths that get that far.
        return {};
    }
    return authority.submit(request);
}

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
