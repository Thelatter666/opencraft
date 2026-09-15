#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/item_stack.hpp"

namespace opencraft::game {

// ── Slot layout (docs/01 §5 ⚖: 主 27 + 快捷栏 9 + 盔甲 4 + 副手 1) ─────────
//
// One flat slot number covers all 41 cells, with the sections laid out
// contiguously so a section is a [begin, end) range and no lookup is needed:
//
//     0 ..  8   Hotbar   (9)   hotbar key k (1-based in the UI) is slot k-1
//     9 .. 35   Main    (27)
//    36 .. 39   Armor    (4)   36 = Head, 37 = Chest, 38 = Legs, 39 = Feet
//    40         Offhand  (1)
//
// The hotbar coming first is what makes "the selected hotbar cell" a plain
// index into this same numbering for the wiring card.
enum class InventorySection : std::uint8_t {
    Hotbar,
    Main,
    Armor,
    Offhand,
};

inline constexpr int kHotbarSlots = 9;
inline constexpr int kMainSlots = 27;
inline constexpr int kArmorSlots = 4;
inline constexpr int kOffhandSlots = 1;

inline constexpr int kHotbarFirstSlot = 0;
inline constexpr int kMainFirstSlot = kHotbarFirstSlot + kHotbarSlots;
inline constexpr int kArmorFirstSlot = kMainFirstSlot + kMainSlots;
inline constexpr int kOffhandFirstSlot = kArmorFirstSlot + kArmorSlots;
inline constexpr int kInventorySlots = kOffhandFirstSlot + kOffhandSlots;

// Armour pieces in slot order: armour slot (kArmorFirstSlot + k) takes the
// item whose EquipSlot is equip_slot_for(ArmorSlot(k)).
enum class ArmorSlot : std::uint8_t {
    Head = 0,
    Chest,
    Legs,
    Feet,
};

[[nodiscard]] constexpr int armor_slot_index(ArmorSlot piece) {
    return kArmorFirstSlot + static_cast<int>(piece);
}

[[nodiscard]] constexpr EquipSlot equip_slot_for(ArmorSlot piece) {
    switch (piece) {
    case ArmorSlot::Head:
        return EquipSlot::Head;
    case ArmorSlot::Chest:
        return EquipSlot::Chest;
    case ArmorSlot::Legs:
        return EquipSlot::Legs;
    case ArmorSlot::Feet:
        return EquipSlot::Feet;
    }
    return EquipSlot::None;
}

// The armour piece an EquipSlot names; nullopt for EquipSlot::None.
[[nodiscard]] constexpr std::optional<ArmorSlot> armor_piece_of(EquipSlot equip) {
    switch (equip) {
    case EquipSlot::Head:
        return ArmorSlot::Head;
    case EquipSlot::Chest:
        return ArmorSlot::Chest;
    case EquipSlot::Legs:
        return ArmorSlot::Legs;
    case EquipSlot::Feet:
        return ArmorSlot::Feet;
    case EquipSlot::None:
        break;
    }
    return std::nullopt;
}

struct SlotRange {
    int begin = 0;
    int end = 0;

    [[nodiscard]] constexpr int size() const { return end - begin; }

    [[nodiscard]] friend constexpr bool operator==(const SlotRange &lhs, const SlotRange &rhs) = default;
};

[[nodiscard]] constexpr SlotRange section_range(InventorySection section) {
    switch (section) {
    case InventorySection::Hotbar:
        return {kHotbarFirstSlot, kMainFirstSlot};
    case InventorySection::Main:
        return {kMainFirstSlot, kArmorFirstSlot};
    case InventorySection::Armor:
        return {kArmorFirstSlot, kOffhandFirstSlot};
    case InventorySection::Offhand:
        return {kOffhandFirstSlot, kInventorySlots};
    }
    return {0, 0};
}

// The sections an item picked up by the player lands in: hotbar then main.
// The armour and offhand cells are only written on purpose (equipping), never
// as spill-over from a pickup.
[[nodiscard]] constexpr SlotRange storage_range() {
    return {kHotbarFirstSlot, kArmorFirstSlot};
}

// Which section a slot belongs to; nullopt when the slot number is invalid.
[[nodiscard]] constexpr std::optional<InventorySection> section_of_slot(int slot) {
    if (slot < kHotbarFirstSlot || slot >= kInventorySlots) {
        return std::nullopt;
    }
    if (slot < kMainFirstSlot) {
        return InventorySection::Hotbar;
    }
    if (slot < kArmorFirstSlot) {
        return InventorySection::Main;
    }
    if (slot < kOffhandFirstSlot) {
        return InventorySection::Armor;
    }
    return InventorySection::Offhand;
}

// ── Operation results ─────────────────────────────────────────────────────

// Outcome of an insert. `added + remaining` is always the count that was
// requested, and the caller's stack is left holding exactly `remaining`.
struct AddResult {
    int added = 0;
    int remaining = 0;

    [[nodiscard]] constexpr int requested() const { return added + remaining; }

    [[nodiscard]] friend constexpr bool operator==(const AddResult &lhs, const AddResult &rhs) = default;
};

// Outcome of a cell-to-cell transfer.
enum class TransferResult : std::uint8_t {
    // The transfer happened (a merge may have moved fewer units than the
    // source held when the destination filled up -- re-read the slots).
    Ok,
    // The destination's slot type refuses the stack. Nothing was changed.
    Rejected,
    // There was nothing to do: same slot, or an empty source.
    NoChange,
};

// ── Inventory ─────────────────────────────────────────────────────────────
//
// The player's 41 cells plus the pure add/remove/move/swap logic over them.
// Holds a reference to the item registry because every insert has to know the
// item's max_stack; the registry outlives the inventory (one per process),
// exactly like MiningTracker's reference to the block registry.
//
// Nothing here is display-, tick- or world-aware: this is the model the
// survival cards (crafting / tools / drops / hunger) are meant to drive.
// Serialisation is not implemented (docs/01 §5 persistence lands with M2c).
class Inventory {
public:
    explicit Inventory(const ItemRegistry &registry) : registry_(&registry) {}

    [[nodiscard]] const ItemRegistry &registry() const { return *registry_; }

    [[nodiscard]] static constexpr bool is_valid_slot(int slot) {
        return slot >= kHotbarFirstSlot && slot < kInventorySlots;
    }

    // Throws std::out_of_range for an invalid slot.
    [[nodiscard]] const ItemStack &slot(int slot) const;

    // All 41 cells in slot order (index == slot number).
    [[nodiscard]] const std::array<ItemStack, kInventorySlots> &slots() const { return slots_; }

    [[nodiscard]] std::optional<InventorySection> section_of(int slot) const { return section_of_slot(slot); }

    // ── Slot-type constraints ──────────────────────────────────────────────
    // Rule (T-I1 acceptance 5, docs/01 §5):
    //   * Hotbar / Main / Offhand accept any item. The offhand's restriction
    //     is structural -- there is exactly one offhand cell (docs/01 §5
    //     "副手 1"), so it can hold at most one stack.
    //   * An armour cell accepts only the one armour piece that belongs to it:
    //     slot 36 needs EquipSlot::Head, 37 Chest, 38 Legs, 39 Feet. A tool, a
    //     block, or another body part's armour is refused.
    //   * An empty stack is accepted by every valid slot, so clearing a cell
    //     is never blocked. An invalid slot number accepts nothing.
    // Violations are refused, never silently relocated: every mutating entry
    // point below reports the refusal instead (see set_slot / move_slot /
    // swap_slots / AddResult).
    [[nodiscard]] bool accepts(int slot, const ItemStack &stack) const;

    // ── Cell access ────────────────────────────────────────────────────────
    // Exact write, for drag-drop, equipping and load-from-save. Returns false
    // and leaves the cell untouched when the slot number is invalid, the slot
    // type refuses the stack, or the stack exceeds max_stack (an oversized
    // stack is refused rather than clamped -- clamping would hide a caller
    // bug; use add_to_slot for the clamping behaviour). Empty stacks are
    // normalised to the default empty stack.
    bool set_slot(int slot, ItemStack stack);

    // Merges up to the item's max_stack into one cell, taking units out of
    // `stack` and leaving the remainder there. Returns how many units went in.
    [[nodiscard]] AddResult add_to_slot(int slot, ItemStack &stack);

    // ── Bulk operations ────────────────────────────────────────────────────
    // Inserts into the storage sections (hotbar then main), in two passes:
    // first top up existing partial stacks of the same item, then open the
    // first empty cells. Nothing spills into the armour or offhand cells.
    // `stack` is left holding what did not fit.
    [[nodiscard]] AddResult add_item(ItemStack &stack);
    // Same, restricted to one section (e.g. equipping into Armor, or filling
    // only the hotbar). Within the section the order is the same.
    [[nodiscard]] AddResult add_item(ItemStack &stack, InventorySection section);

    // Removes up to `count` units of `item`, draining cells in ascending slot
    // order across all 41 cells. Returns how many units were actually
    // removed. Non-positive counts and the empty id remove nothing.
    int remove_item(std::uint16_t item, int count);
    // Removes up to `count` units from a single cell. Returns the number
    // removed; 0 for an invalid slot.
    int remove_from_slot(int slot, int count);

    // ── Cell-to-cell transfers ─────────────────────────────────────────────
    // Moves as much of `from` into `to` as fits: an empty destination takes
    // the stack, the same item merges up to max_stack (a partial move, the
    // rest stays in `from`), and a destination holding a different item is
    // Rejected -- use swap_slots to exchange two different items.
    [[nodiscard]] TransferResult move_slot(int from, int to);
    // Exchanges two cells. Atomic: when either direction would violate a slot
    // type constraint, both cells are left exactly as they were.
    [[nodiscard]] TransferResult swap_slots(int first, int second);

    // ── Queries ────────────────────────────────────────────────────────────
    [[nodiscard]] int count_of(std::uint16_t item) const;
    [[nodiscard]] int count_in_section(std::uint16_t item, InventorySection section) const;
    [[nodiscard]] bool is_empty() const;
    // Cells holding something.
    [[nodiscard]] int used_slots() const;

    // Compares the cells only; the registry reference is not part of the
    // value (two inventories over the same items are equal).
    [[nodiscard]] friend bool operator==(const Inventory &lhs, const Inventory &rhs) {
        return lhs.slots_ == rhs.slots_;
    }

private:
    [[nodiscard]] AddResult add_item(ItemStack &stack, SlotRange range);

    const ItemRegistry *registry_;
    std::array<ItemStack, kInventorySlots> slots_{};
};

} // namespace opencraft::game
