#include "opencraft/game/protocol.hpp"

namespace opencraft::game {

const char *action_reject_reason(const ActionReject reject) {
    switch (reject) {
    case ActionReject::None:
        return "";
    case ActionReject::OutOfWorld:
        return "target outside the world";
    case ActionReject::ChunkNotLoaded:
        return "target chunk not loaded";
    case ActionReject::OutOfReach:
        return "target out of reach";
    case ActionReject::NothingToDig:
        return "no block to dig";
    case ActionReject::Unbreakable:
        return "block is unbreakable";
    case ActionReject::UnknownBlock:
        return "not a placeable block";
    case ActionReject::CellOccupied:
        return "cell is occupied";
    case ActionReject::IntersectsActor:
        return "cell overlaps the actor";
    case ActionReject::NotAWaterSource:
        return "no water source";
    }
    return "";
}

} // namespace opencraft::game
