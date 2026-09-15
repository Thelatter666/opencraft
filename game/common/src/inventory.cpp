#include "opencraft/game/inventory.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace opencraft::game {

const ItemStack &Inventory::slot(int slot_number) const {
    if (!is_valid_slot(slot_number)) {
        throw std::out_of_range("inventory slot out of range");
    }
    return slots_[static_cast<std::size_t>(slot_number)];
}

bool Inventory::accepts(int slot_number, const ItemStack &stack) const {
    if (!is_valid_slot(slot_number)) {
        return false;
    }
    // An empty stack clears a cell, which every slot type allows.
    if (stack.empty()) {
        return true;
    }
    const auto section = section_of_slot(slot_number);
    switch (*section) {
    case InventorySection::Hotbar:
    case InventorySection::Main:
    case InventorySection::Offhand:
        return true;
    case InventorySection::Armor: {
        const auto piece = static_cast<ArmorSlot>(slot_number - kArmorFirstSlot);
        return registry_->def_of(stack.item).equip == equip_slot_for(piece);
    }
    }
    return false;
}

bool Inventory::set_slot(int slot_number, ItemStack stack) {
    if (!accepts(slot_number, stack)) {
        return false;
    }
    if (!stack.empty() && stack.count > stack_limit_of(*registry_, stack.item)) {
        return false;
    }
    slots_[static_cast<std::size_t>(slot_number)] = stack.empty() ? ItemStack{} : stack;
    return true;
}

AddResult Inventory::add_to_slot(int slot_number, ItemStack &stack) {
    if (stack.empty()) {
        return {};
    }
    if (!accepts(slot_number, stack)) {
        return {0, stack.count};
    }
    ItemStack &cell = slots_[static_cast<std::size_t>(slot_number)];
    // merge_from adopts the item into an empty cell, or tops up a cell of the
    // same item; either way it stops at the item's max_stack.
    const int added = cell.merge_from(stack, stack_limit_of(*registry_, stack.item));
    return {added, stack.count};
}

AddResult Inventory::add_item(ItemStack &stack) {
    return add_item(stack, storage_range());
}

AddResult Inventory::add_item(ItemStack &stack, InventorySection section) {
    return add_item(stack, section_range(section));
}

AddResult Inventory::add_item(ItemStack &stack, SlotRange range) {
    if (stack.empty()) {
        return {};
    }
    const int requested = stack.count;
    // Pass 1: top up existing partial stacks of the same item. Doing this as
    // its own pass (rather than one pass that also fills the first empty
    // cell) is what makes "merge before opening a new cell" hold even when an
    // empty cell sits to the left of a partial one.
    for (int slot_number = range.begin; slot_number < range.end && !stack.empty(); ++slot_number) {
        if (slots_[static_cast<std::size_t>(slot_number)].same_item(stack)) {
            static_cast<void>(add_to_slot(slot_number, stack));
        }
    }
    // Pass 2: fill empty cells the slot type accepts, in slot order.
    for (int slot_number = range.begin; slot_number < range.end && !stack.empty(); ++slot_number) {
        if (slots_[static_cast<std::size_t>(slot_number)].empty() && accepts(slot_number, stack)) {
            static_cast<void>(add_to_slot(slot_number, stack));
        }
    }
    return {requested - stack.count, stack.count};
}

int Inventory::remove_item(std::uint16_t item, int count) {
    if (count <= 0 || item == ItemRegistry::kEmptyId) {
        return 0;
    }
    int removed = 0;
    for (int slot_number = kHotbarFirstSlot; slot_number < kInventorySlots && removed < count; ++slot_number) {
        ItemStack &cell = slots_[static_cast<std::size_t>(slot_number)];
        if (cell.item != item) {
            continue;
        }
        removed += cell.shrink(count - removed);
    }
    return removed;
}

int Inventory::remove_from_slot(int slot_number, int count) {
    if (!is_valid_slot(slot_number)) {
        return 0;
    }
    return slots_[static_cast<std::size_t>(slot_number)].shrink(count);
}

TransferResult Inventory::move_slot(int from, int to) {
    if (!is_valid_slot(from) || !is_valid_slot(to)) {
        return TransferResult::Rejected;
    }
    if (from == to) {
        return TransferResult::NoChange;
    }
    ItemStack &source = slots_[static_cast<std::size_t>(from)];
    if (source.empty()) {
        return TransferResult::NoChange;
    }
    if (!accepts(to, source)) {
        return TransferResult::Rejected;
    }
    ItemStack &destination = slots_[static_cast<std::size_t>(to)];
    if (destination.empty() || destination.same_item(source)) {
        // Limit comes from the moved item, not from the (possibly empty)
        // destination cell, whose stack_limit() would be 0.
        destination.merge_from(source, stack_limit_of(*registry_, source.item));
        return TransferResult::Ok;
    }
    // A different item in the destination is not a move; swapping is the
    // operation the caller wants (swap_slots).
    return TransferResult::Rejected;
}

TransferResult Inventory::swap_slots(int first, int second) {
    if (!is_valid_slot(first) || !is_valid_slot(second)) {
        return TransferResult::Rejected;
    }
    if (first == second) {
        return TransferResult::NoChange;
    }
    ItemStack &a = slots_[static_cast<std::size_t>(first)];
    ItemStack &b = slots_[static_cast<std::size_t>(second)];
    if (a.empty() && b.empty()) {
        return TransferResult::NoChange;
    }
    // Both directions have to be legal before anything moves, so a refused
    // swap cannot leave the inventory half-exchanged.
    if (!accepts(first, b) || !accepts(second, a)) {
        return TransferResult::Rejected;
    }
    std::swap(a, b);
    return TransferResult::Ok;
}

int Inventory::count_of(std::uint16_t item) const {
    if (item == ItemRegistry::kEmptyId) {
        return 0;
    }
    int total = 0;
    for (const ItemStack &cell : slots_) {
        if (cell.item == item) {
            total += cell.count;
        }
    }
    return total;
}

int Inventory::count_in_section(std::uint16_t item, InventorySection section) const {
    if (item == ItemRegistry::kEmptyId) {
        return 0;
    }
    const SlotRange range = section_range(section);
    int total = 0;
    for (int slot_number = range.begin; slot_number < range.end; ++slot_number) {
        const ItemStack &cell = slots_[static_cast<std::size_t>(slot_number)];
        if (cell.item == item) {
            total += cell.count;
        }
    }
    return total;
}

bool Inventory::is_empty() const {
    return std::all_of(slots_.begin(), slots_.end(), [](const ItemStack &cell) { return cell.empty(); });
}

int Inventory::used_slots() const {
    return static_cast<int>(
        std::count_if(slots_.begin(), slots_.end(), [](const ItemStack &cell) { return !cell.empty(); }));
}

} // namespace opencraft::game
