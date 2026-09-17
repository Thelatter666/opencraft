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
    case ActionReject::UnknownEntity:
        return "no such dropped item";
    case ActionReject::EntityItemMismatch:
        return "dropped item holds a different item";
    case ActionReject::PickupDelayActive:
        return "dropped item cannot be picked up yet";
    case ActionReject::OutOfPickupRange:
        return "dropped item out of pickup range";
    case ActionReject::EntityNotLoaded:
        return "dropped item is in an unloaded chunk";
    case ActionReject::NotAMob:
        return "target entity is not a mob";
    case ActionReject::OutOfAttackRange:
        return "target out of melee reach";
    case ActionReject::NotBreedable:
        return "that mob cannot be bred";
    case ActionReject::WrongFood:
        return "held item is not that mob's food";
    case ActionReject::MobNotAdult:
        return "a baby cannot breed";
    case ActionReject::BreedingCooldown:
        return "mob is still on its breeding cooldown";
    case ActionReject::UnknownItem:
        return "no such item";
    case ActionReject::BadStackCount:
        return "stack count out of range";
    }
    return "";
}

} // namespace opencraft::game
