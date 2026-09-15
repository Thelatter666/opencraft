#pragma once

#include <algorithm>
#include <cstdint>

#include "opencraft/game/item_registry.hpp"

namespace opencraft::game {

// The maximum count a single cell of `item` may hold. 0 for the reserved
// empty id; throws std::out_of_range for an unknown numeric id (same
// contract as ItemRegistry::def_of).
[[nodiscard]] inline int stack_limit_of(const ItemRegistry &registry, std::uint16_t item) {
    if (item == ItemRegistry::kEmptyId) {
        return 0;
    }
    return registry.def_of(item).max_stack;
}

// One inventory cell's worth of an item: which item, how many. Pure data, and
// every operation here either only observes or mutates just the stack it is
// given -- no registry mutation, no I/O, no world access.
//
// Invariant for every stack this library produces: empty() is exactly
// (item == kEmptyId), and count never exceeds the item's max_stack. A
// hand-built stack may break it (e.g. count == 0 with a named item); such a
// stack counts as empty everywhere, and Inventory normalises it on write --
// which is why oversized stacks are refused rather than silently clamped.
struct ItemStack {
    std::uint16_t item = ItemRegistry::kEmptyId;
    int count = 0;

    // Builds a normalised stack: no item id, or a non-positive count,
    // collapses to the empty stack.
    [[nodiscard]] static ItemStack of(std::uint16_t item, int count) {
        if (item == ItemRegistry::kEmptyId || count <= 0) {
            return {};
        }
        return {item, count};
    }

    [[nodiscard]] bool empty() const { return item == ItemRegistry::kEmptyId || count <= 0; }

    // Same named item and both non-empty: the condition for two stacks to be
    // able to share one cell. Nothing else is compared -- a stack carries no
    // state beyond item and count.
    [[nodiscard]] bool same_item(const ItemStack &other) const {
        return !empty() && !other.empty() && item == other.item;
    }

    // The free-standing spelling of same_item, for call sites that read
    // better as a question about two stacks than as a member call.
    [[nodiscard]] static bool can_merge(const ItemStack &a, const ItemStack &b) { return a.same_item(b); }

    // The maximum count a cell of this stack's item may hold; 0 when empty.
    [[nodiscard]] int stack_limit(const ItemRegistry &registry) const { return stack_limit_of(registry, item); }

    // Moves up to (limit - count) units out of `other` into this stack.
    // Returns the number of units moved; `other` is drained by that much and
    // empties itself once it runs out. A stack with no item adopts `other`'s
    // item first, so an empty cell can be filled with this one call.
    int merge_from(ItemStack &other, int limit) {
        if (other.empty() || limit <= 0) {
            return 0;
        }
        if (empty()) {
            item = other.item;
            count = 0;
        } else if (item != other.item) {
            return 0;
        }
        const int moved = std::min(limit - count, other.count);
        if (moved <= 0) {
            return 0;
        }
        count += moved;
        other.count -= moved;
        if (other.count <= 0) {
            other = ItemStack{};
        }
        return moved;
    }

    // Removes up to `amount` units and returns them as a new stack, leaving
    // the remainder here. A non-positive amount takes nothing; the returned
    // stack is empty when there was nothing to take.
    [[nodiscard]] ItemStack split(int amount) {
        if (amount <= 0 || empty()) {
            return {};
        }
        const int taken = std::min(amount, count);
        ItemStack taken_stack{item, taken};
        count -= taken;
        if (count <= 0) {
            *this = ItemStack{};
        }
        return taken_stack;
    }

    // Drops up to `amount` units, returning how many were actually dropped.
    int shrink(int amount) {
        if (amount <= 0 || empty()) {
            return 0;
        }
        const int removed = std::min(amount, count);
        count -= removed;
        if (count <= 0) {
            *this = ItemStack{};
        }
        return removed;
    }

    // Raw field comparison. Because every library entry point normalises,
    // this is also "same contents": an empty cell always equals the default
    // ItemStack no matter how it was emptied.
    [[nodiscard]] friend bool operator==(const ItemStack &lhs, const ItemStack &rhs) = default;
};

} // namespace opencraft::game
