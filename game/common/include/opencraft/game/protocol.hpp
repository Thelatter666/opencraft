#pragma once

// T-A1: the request/result vocabulary of the authoritative split, shared by
// both ends (docs/03 §1: game/common carries the 协议消息定义).
//
// Everything here is plain value data - no pointers, no callbacks, no shared
// mutable state - so the in-process channel this card builds can be replaced by
// a serializing network channel in M3 without touching the call sites.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace opencraft::game {

// ⚖ docs/01 §4: survival block reach, in blocks. One copy for both ends: the
// client aims with it and the authoritative side re-checks it (T-A1).
inline constexpr double kReachDistance = 4.5;

// What the client asks the authoritative side to do - one entry per
// world-changing player action in T-A1's scope.
//
// The inventory is deliberately not in this vocabulary: a request carries the
// block to place (the T-A1 card's sketch does the same), so the authority
// decides the world outcome from its own data only. Moving the inventory across
// the channel belongs to M3, together with the player/session.
enum class ActionKind : std::uint8_t {
    Dig,        // break the block at `target` into air
    PlaceBlock, // write `item_or_block` at `target`
    PourWater,  // place a water source at `target`
    ScoopWater, // remove the water source at `target`
};

// The actor geometry the authority validates against. Value data on purpose:
// over a network these are the fields the client claims and the server
// re-checks against its own copy of the player.
struct ActorPose {
    glm::dvec3 feet{0.0, 0.0, 0.0}; // feet centre, same convention as PlayerState::position
    double height = 1.8;            // current pose height (1.8 standing / 1.5 sneaking)
    double eye_height = 1.62;       // eye above the feet; the authority derives the eye itself
};

struct ActionRequest {
    ActionKind kind = ActionKind::Dig;
    glm::ivec3 target{0, 0, 0};
    std::uint16_t item_or_block = 0; // PlaceBlock: block id of the held stack
    ActorPose actor{};
    std::uint32_t sequence = 0; // reserved for M3 prediction/reconciliation; unused in this card
};

// Why a request was refused. A stable code rather than a prose message: the
// tests match on it and M3 ships it to the player as feedback (the prose comes
// from action_reject_reason()).
enum class ActionReject : std::uint8_t {
    None = 0,        // accepted
    OutOfWorld,      // target y outside [0, Chunk::kSizeY)
    ChunkNotLoaded,  // the target's chunk is not in memory: the write would be dropped
    OutOfReach,      // target outside the actor's ⚖ reach (kReachDistance)
    NothingToDig,    // Dig on air
    Unbreakable,     // Dig on a block with hardness < 0 (bedrock)
    UnknownBlock,    // PlaceBlock with an id the registry does not know, or with air
    CellOccupied,    // PlaceBlock / PourWater into a cell that is not replaceable
    IntersectsActor, // PlaceBlock into the actor's own AABB
    NotAWaterSource, // ScoopWater where there is no source
};

[[nodiscard]] const char *action_reject_reason(ActionReject reject);

struct ActionResult {
    bool accepted = false;
    ActionReject reject = ActionReject::None;

    // Human-readable form of `reject`; "" while accepted.
    [[nodiscard]] const char *reason() const { return action_reject_reason(reject); }
};

// Authoritative -> client push-back, drained once per tick: the derived work
// the client must redo because the authority changed something.
//
// It carries no block deltas on purpose. In-process the client renders the
// authority's own storage through a read-only view (client::WorldSource), so
// block and fluid *state* needs no shipping - only the "what is stale now"
// list does. The day a copy exists on the client (M3, with prediction), the
// same struct grows the per-block events.
struct WorldChanges {
    // Chunks whose baked mesh is stale (own chunk + light/fluid neighbors).
    std::vector<std::pair<int, int>> dirty_chunks;

    void clear() { dirty_chunks.clear(); }

    [[nodiscard]] bool empty() const { return dirty_chunks.empty(); }
};

// The client's handle on the authoritative side - the whole of it. Everything
// the client is allowed to do to the world goes through these four verbs;
// server::WorldSim is this card's in-process implementation, and M3 swaps in
// one that serializes `submit()` onto a socket (its world already lives
// elsewhere, so the other three become message pumps).
class IAuthority {
public:
    virtual ~IAuthority() = default;

    // Executes one request, or refuses it with a reason. The world write (if
    // any) has already happened when this returns.
    [[nodiscard]] virtual ActionResult submit(const ActionRequest &req) = 0;

    // One authoritative simulation step (the fluid scheduled ticks). The
    // authority runs inside the client process for now, so the client's fixed
    // step drives it; a standalone server owns its own 20 TPS loop in M3.
    virtual void tick() = 0;

    // Takes everything changed since the previous call; the authority's buffer
    // is emptied, so each change is reported exactly once.
    [[nodiscard]] virtual WorldChanges take_changes() = 0;

    // Persistence pump (T009 cadence): hands the attached save a snapshot of
    // every dirty chunk and returns how many were queued. Not a world write -
    // it is the authority's own maintenance, still driven from the client's
    // tick in this card because the embedded server shares its clock.
    virtual std::size_t autosave_pass() = 0;
};

} // namespace opencraft::game
