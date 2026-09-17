#pragma once

// The gamerule skeleton (T-D45 §2.6). ⚖ docs/01 §7 asks for a registry of ~15
// boolean/integer rules saved with the level; this card adds exactly ONE of
// them, because death-without-consequence is what blocks the survival loop and
// the other ~14 rules are a card of their own.
//
// Where it lives: on the CLIENT, next to the only thing that reads it. The one
// rule here - keepInventory - changes what a death does to the inventory, and
// the inventory (like the player's hit points) is the client's until M3 moves
// the session up to the authority (game/protocol.hpp's ActorEvent comment).
// A registry with real persistence and a second rule belongs on the authority,
// together with the rest of the player state; moving it early would be a second
// copy of a thing M3 will own.

namespace opencraft::client {

// ⚖ docs/01 §7 lists keepInventory among the rules the game ships; the DEFAULT
// this card uses (false = drop everything) is the base game's default, and the
// research material that defines the rule's further effects is thin
// (docs/research/11 §3.5 says only "为 true 时经验也保留", and there is no
// experience system yet - that half is out of scope per the card's §1).
struct GameRules {
    bool keep_inventory = false;
};

} // namespace opencraft::client
