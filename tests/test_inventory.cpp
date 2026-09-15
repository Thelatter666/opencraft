#include <cstdint>
#include <stdexcept>

#include <doctest/doctest.h>

#include "opencraft/game/inventory.hpp"
#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/item_stack.hpp"

namespace {

using opencraft::game::AddResult;
using opencraft::game::armor_piece_of;
using opencraft::game::armor_slot_index;
using opencraft::game::ArmorSlot;
using opencraft::game::equip_slot_for;
using opencraft::game::EquipSlot;
using opencraft::game::Inventory;
using opencraft::game::InventorySection;
using opencraft::game::ItemRegistry;
using opencraft::game::ItemStack;
using opencraft::game::kArmorFirstSlot;
using opencraft::game::kArmorSlots;
using opencraft::game::kHotbarFirstSlot;
using opencraft::game::kHotbarSlots;
using opencraft::game::kInventorySlots;
using opencraft::game::kMainFirstSlot;
using opencraft::game::kMainSlots;
using opencraft::game::kOffhandFirstSlot;
using opencraft::game::kOffhandSlots;
using opencraft::game::kStackLimitLarge;
using opencraft::game::kStackLimitMedium;
using opencraft::game::section_of_slot;
using opencraft::game::section_range;
using opencraft::game::SlotRange;
using opencraft::game::storage_range;
using opencraft::game::TransferResult;

// Registry plus the handful of items these tests need, so a test reads as the
// situation it describes instead of as numeric ids.
struct World {
    ItemRegistry registry = ItemRegistry::create_default();
    Inventory inventory{registry};

    std::uint16_t clod = registry.id_of("loam_clod");      // stacks to 64
    std::uint16_t rock = registry.id_of("greyrock");       // stacks to 64
    std::uint16_t vessel = registry.id_of("empty_vessel"); // stacks to 16
    std::uint16_t tool = registry.id_of("timber_chisel");  // stacks to 1
    std::uint16_t head = registry.id_of("timber_headguard");
    std::uint16_t chest = registry.id_of("timber_cuirass");
    std::uint16_t legs = registry.id_of("timber_greaves");
    std::uint16_t feet = registry.id_of("timber_treads");

    [[nodiscard]] ItemStack stack(std::uint16_t item, int count) const { return ItemStack::of(item, count); }

    // Pick-up path: inserts into the storage sections.
    AddResult add(std::uint16_t item, int count) {
        ItemStack incoming = stack(item, count);
        return inventory.add_item(incoming);
    }

    // Purposeful insert into one section (equipping, filling the hotbar).
    AddResult add(std::uint16_t item, int count, InventorySection section) {
        ItemStack incoming = stack(item, count);
        return inventory.add_item(incoming, section);
    }

    // Direct cell write, for setting up a situation.
    bool put(int slot, std::uint16_t item, int count) { return inventory.set_slot(slot, stack(item, count)); }
};

} // namespace

TEST_CASE("inventory layout is the 9/27/4/1 split of the spec") {
    CHECK(kHotbarSlots == 9);
    CHECK(kMainSlots == 27);
    CHECK(kArmorSlots == 4);
    CHECK(kOffhandSlots == 1);
    CHECK(kInventorySlots == 41);

    CHECK(kHotbarFirstSlot == 0);
    CHECK(kMainFirstSlot == 9);
    CHECK(kArmorFirstSlot == 36);
    CHECK(kOffhandFirstSlot == 40);

    CHECK(section_range(InventorySection::Hotbar) == SlotRange{0, 9});
    CHECK(section_range(InventorySection::Main) == SlotRange{9, 36});
    CHECK(section_range(InventorySection::Armor) == SlotRange{36, 40});
    CHECK(section_range(InventorySection::Offhand) == SlotRange{40, 41});
    CHECK(storage_range() == SlotRange{0, 36});

    CHECK(section_of_slot(0) == InventorySection::Hotbar);
    CHECK(section_of_slot(8) == InventorySection::Hotbar);
    CHECK(section_of_slot(9) == InventorySection::Main);
    CHECK(section_of_slot(35) == InventorySection::Main);
    CHECK(section_of_slot(36) == InventorySection::Armor);
    CHECK(section_of_slot(39) == InventorySection::Armor);
    CHECK(section_of_slot(40) == InventorySection::Offhand);
    CHECK_FALSE(section_of_slot(-1).has_value());
    CHECK_FALSE(section_of_slot(kInventorySlots).has_value());

    CHECK(armor_slot_index(ArmorSlot::Head) == kArmorFirstSlot + 0);
    CHECK(armor_slot_index(ArmorSlot::Chest) == kArmorFirstSlot + 1);
    CHECK(armor_slot_index(ArmorSlot::Legs) == kArmorFirstSlot + 2);
    CHECK(armor_slot_index(ArmorSlot::Feet) == kArmorFirstSlot + 3);
    CHECK(equip_slot_for(ArmorSlot::Legs) == EquipSlot::Legs);
    CHECK(armor_piece_of(EquipSlot::Chest) == ArmorSlot::Chest);
    CHECK_FALSE(armor_piece_of(EquipSlot::None).has_value());
}

TEST_CASE("a fresh inventory has 41 empty cells") {
    const World world;
    const auto &inventory = world.inventory;
    CHECK(inventory.is_empty());
    CHECK(inventory.used_slots() == 0);
    CHECK(inventory.count_of(world.clod) == 0);
    CHECK(inventory.slots().size() == static_cast<std::size_t>(kInventorySlots));
    for (int slot = 0; slot < kInventorySlots; ++slot) {
        INFO("slot " << slot);
        CHECK(inventory.slot(slot).empty());
    }
    CHECK_THROWS_AS([&] { static_cast<void>(inventory.slot(kInventorySlots)); }(), std::out_of_range);
    CHECK_THROWS_AS([&] { static_cast<void>(inventory.slot(-1)); }(), std::out_of_range);
    CHECK_FALSE(Inventory::is_valid_slot(-1));
    CHECK(Inventory::is_valid_slot(0));
    CHECK(Inventory::is_valid_slot(kInventorySlots - 1));
    CHECK_FALSE(Inventory::is_valid_slot(kInventorySlots));
    CHECK(&inventory.registry() == &world.registry);
}

TEST_CASE("add_item tops up a partial stack before opening a new cell") {
    World world;
    auto &inventory = world.inventory;
    ItemStack sixty = world.stack(world.clod, 60);
    CHECK(inventory.add_to_slot(10, sixty) == AddResult{60, 0});
    CHECK(inventory.slot(0).empty());

    ItemStack incoming = world.stack(world.clod, 40);
    const AddResult result = inventory.add_item(incoming);
    CHECK(result == AddResult{40, 0});
    CHECK(result.requested() == 40);
    CHECK(incoming.empty());
    CHECK(inventory.slot(10).count == kStackLimitLarge); // pass 1 topped the partial stack up
    CHECK(inventory.slot(0).count == 36);                // pass 2 used the first empty cell
    CHECK(inventory.count_of(world.clod) == 100);
    CHECK(inventory.used_slots() == 2);
}

TEST_CASE("add_item fills cells in slot order and only opens a new cell when needed") {
    World world;
    auto &inventory = world.inventory;
    CHECK(world.add(world.clod, kStackLimitLarge) == AddResult{64, 0});
    CHECK(world.add(world.rock, kStackLimitLarge) == AddResult{64, 0});
    CHECK(inventory.slot(0).item == world.clod);
    CHECK(inventory.slot(1).item == world.rock);

    // The first cell of the same item is full, so a new cell opens after it.
    CHECK(world.add(world.clod, 20) == AddResult{20, 0});
    CHECK(inventory.slot(0).count == kStackLimitLarge);
    CHECK(inventory.slot(2).count == 20);
    CHECK(inventory.slot(3).empty());

    // The next insert tops that partial stack up first, then opens cell 3.
    CHECK(world.add(world.clod, 80) == AddResult{80, 0});
    CHECK(inventory.slot(2).count == kStackLimitLarge);
    CHECK(inventory.slot(3).count == 36);
    CHECK(inventory.count_of(world.clod) == 164);
    CHECK(inventory.count_in_section(world.clod, InventorySection::Hotbar) == 164);
    CHECK(inventory.count_in_section(world.clod, InventorySection::Main) == 0);
    CHECK(inventory.used_slots() == 4);
}

TEST_CASE("add_item respects each of the three stack limits") {
    World world;
    auto &inventory = world.inventory;

    // 64-tier: a request larger than one cell spills into the next cell, and
    // no cell ever goes above the limit.
    CHECK(world.add(world.clod, 100) == AddResult{100, 0});
    CHECK(inventory.slot(0).count == kStackLimitLarge);
    CHECK(inventory.slot(1).count == 36);
    CHECK(inventory.count_of(world.clod) == 100);

    // 16-tier.
    CHECK(world.add(world.vessel, 20) == AddResult{20, 0});
    CHECK(inventory.slot(2).count == 16);
    CHECK(inventory.slot(3).count == 4);

    // 1-tier: every unit needs a cell of its own.
    CHECK(world.add(world.tool, 3) == AddResult{3, 0});
    CHECK(inventory.slot(4).count == 1);
    CHECK(inventory.slot(5).count == 1);
    CHECK(inventory.slot(6).count == 1);
    CHECK(inventory.used_slots() == 7);

    // Once no cell can take any more, the request comes back as remaining.
    for (int slot = 7; slot < 36; ++slot) {
        CHECK(world.put(slot, world.tool, 1));
    }
    CHECK(world.add(world.tool, 2) == AddResult{0, 2});
    CHECK(inventory.count_of(world.tool) == 32); // 3 cells above plus 29 more
}

TEST_CASE("add_item hands back the whole remainder once the storage section is full") {
    World world;
    auto &inventory = world.inventory;
    CHECK(world.add(world.clod, kStackLimitLarge * 36) == AddResult{64 * 36, 0});
    CHECK(inventory.count_of(world.clod) == 64 * 36);
    CHECK(inventory.used_slots() == 36);
    // Pick-ups never land in the armour or offhand cells.
    CHECK(inventory.count_in_section(world.clod, InventorySection::Armor) == 0);
    CHECK(inventory.count_in_section(world.clod, InventorySection::Offhand) == 0);

    ItemStack overflow = world.stack(world.rock, 100);
    CHECK(inventory.add_item(overflow) == AddResult{0, 100});
    CHECK(overflow.count == 100);
    CHECK(inventory.slot(kOffhandFirstSlot).empty());
}

TEST_CASE("add_item ignores an empty or non-positive request") {
    World world;
    auto &inventory = world.inventory;
    ItemStack nothing;
    CHECK(inventory.add_item(nothing) == AddResult{0, 0});
    CHECK(world.add(world.clod, 0) == AddResult{0, 0});
    CHECK(world.add(world.clod, -5) == AddResult{0, 0});
    CHECK(inventory.is_empty());
}

TEST_CASE("add_item into a section never spills into another section") {
    World world;
    auto &inventory = world.inventory;
    CHECK(world.add(world.clod, kStackLimitLarge * kHotbarSlots + 30, InventorySection::Hotbar) ==
          AddResult{64 * 9, 30});
    CHECK(inventory.count_in_section(world.clod, InventorySection::Hotbar) == 64 * 9);
    CHECK(inventory.count_in_section(world.clod, InventorySection::Main) == 0);
    CHECK(inventory.slot(kHotbarSlots - 1).count == kStackLimitLarge);
    CHECK(inventory.slot(kMainFirstSlot).empty());

    // A section-scoped request that the section cannot take comes back whole.
    CHECK(world.add(world.rock, 5, InventorySection::Armor) == AddResult{0, 5});
    CHECK(inventory.count_of(world.rock) == 0);
}

TEST_CASE("armour cells accept exactly their own armour piece") {
    World world;
    auto &inventory = world.inventory;

    CHECK(world.add(world.head, 1, InventorySection::Armor) == AddResult{1, 0});
    CHECK(world.add(world.chest, 1, InventorySection::Armor) == AddResult{1, 0});
    CHECK(world.add(world.legs, 1, InventorySection::Armor) == AddResult{1, 0});
    CHECK(world.add(world.feet, 1, InventorySection::Armor) == AddResult{1, 0});
    CHECK(inventory.slot(kArmorFirstSlot + 0).item == world.head);
    CHECK(inventory.slot(kArmorFirstSlot + 1).item == world.chest);
    CHECK(inventory.slot(kArmorFirstSlot + 2).item == world.legs);
    CHECK(inventory.slot(kArmorFirstSlot + 3).item == world.feet);
    CHECK(inventory.count_in_section(world.head, InventorySection::Armor) == 1);

    // A second headguard has nowhere to go: the other armour cells refuse it
    // and the head cell is full (max_stack 1).
    CHECK(world.add(world.head, 1, InventorySection::Armor) == AddResult{0, 1});

    // Right armour class, wrong body part: still refused.
    CHECK_FALSE(inventory.accepts(kArmorFirstSlot + 1, world.stack(world.head, 1)));
    CHECK_FALSE(inventory.set_slot(kArmorFirstSlot + 1, world.stack(world.head, 1)));
    CHECK(inventory.slot(kArmorFirstSlot + 1).item == world.chest); // untouched by the refusal
    CHECK(inventory.accepts(kArmorFirstSlot, world.stack(world.head, 1)));

    // Non-armour items are refused by every armour cell.
    CHECK_FALSE(inventory.accepts(kArmorFirstSlot, world.stack(world.clod, 1)));
    CHECK_FALSE(inventory.accepts(kArmorFirstSlot + 3, world.stack(world.tool, 1)));
    CHECK(world.add(world.tool, 1, InventorySection::Armor) == AddResult{0, 1});
    CHECK_FALSE(world.put(kArmorFirstSlot, world.clod, 1));

    // Every other cell takes any item, and any cell can be cleared.
    CHECK(inventory.accepts(0, world.stack(world.clod, 1)));
    CHECK(inventory.accepts(kOffhandFirstSlot, world.stack(world.head, 1)));
    CHECK(inventory.accepts(kArmorFirstSlot, ItemStack{}));
    CHECK_FALSE(inventory.accepts(-1, world.stack(world.clod, 1)));
    CHECK_FALSE(inventory.accepts(kInventorySlots, world.stack(world.clod, 1)));

    // Armour is removable through the ordinary removal path.
    CHECK(inventory.remove_item(world.head, 1) == 1);
    CHECK(inventory.slot(kArmorFirstSlot).empty());
}

TEST_CASE("the offhand is a single cell that takes any item") {
    World world;
    auto &inventory = world.inventory;
    CHECK(kOffhandSlots == 1);
    CHECK(section_range(InventorySection::Offhand).size() == 1);

    CHECK(world.add(world.clod, 70, InventorySection::Offhand) == AddResult{64, 6});
    CHECK(inventory.slot(kOffhandFirstSlot).count == kStackLimitLarge);
    CHECK(inventory.used_slots() == 1);

    // The cell is occupied, so a second insert has nowhere to go...
    CHECK(world.add(world.head, 1, InventorySection::Offhand) == AddResult{0, 1});
    // ...but it can be replaced directly, with any item.
    CHECK(inventory.set_slot(kOffhandFirstSlot, world.stack(world.head, 1)));
    CHECK(inventory.slot(kOffhandFirstSlot).item == world.head);
    CHECK(inventory.count_of(world.clod) == 0);

    // Offhand-scoped inserts stay out of the hotbar and the main section.
    World fresh;
    CHECK(fresh.add(fresh.rock, 3, InventorySection::Offhand) == AddResult{3, 0});
    CHECK(fresh.inventory.count_in_section(fresh.rock, InventorySection::Hotbar) == 0);
    CHECK(fresh.inventory.count_in_section(fresh.rock, InventorySection::Main) == 0);
    CHECK(fresh.inventory.slot(kOffhandFirstSlot).count == 3);
}

TEST_CASE("move_slot moves or merges same items and refuses a mismatched destination") {
    World world;
    auto &inventory = world.inventory;
    CHECK(world.put(0, world.clod, 20));
    CHECK(world.put(1, world.clod, 50));

    // Same item: merges up to the limit, the overflow stays in the source.
    CHECK(inventory.move_slot(0, 1) == TransferResult::Ok);
    CHECK(inventory.slot(1).count == kStackLimitLarge);
    CHECK(inventory.slot(0).count == 6);

    // A different item in the destination is not a move.
    CHECK(world.put(2, world.rock, 10));
    CHECK(inventory.move_slot(1, 2) == TransferResult::Rejected);
    CHECK(inventory.slot(1).count == kStackLimitLarge);
    CHECK(inventory.slot(2).item == world.rock);
    CHECK(inventory.slot(2).count == 10);

    // An empty destination takes the stack, across sections included.
    CHECK(inventory.move_slot(1, kOffhandFirstSlot) == TransferResult::Ok);
    CHECK(inventory.slot(kOffhandFirstSlot).count == kStackLimitLarge);
    CHECK(inventory.slot(1).empty());

    // Nothing to do, or an invalid slot.
    CHECK(inventory.move_slot(1, 1) == TransferResult::NoChange);
    CHECK(inventory.move_slot(2, 2) == TransferResult::NoChange);
    CHECK(inventory.move_slot(3, 4) == TransferResult::NoChange); // empty source
    CHECK(inventory.move_slot(-1, 0) == TransferResult::Rejected);
    CHECK(inventory.move_slot(0, kInventorySlots) == TransferResult::Rejected);
    CHECK(inventory.slot(0).count == 6); // untouched by the refusals
}

TEST_CASE("move_slot refuses to put a non-armour item into an armour cell") {
    World world;
    auto &inventory = world.inventory;
    CHECK(world.put(0, world.clod, 5));
    CHECK(inventory.move_slot(0, kArmorFirstSlot) == TransferResult::Rejected);
    CHECK(inventory.slot(0).count == 5);
    CHECK(inventory.slot(kArmorFirstSlot).empty());

    // A headguard may leave its cell but may not enter another body part's.
    CHECK(world.put(kArmorFirstSlot, world.head, 1));
    CHECK(inventory.move_slot(kArmorFirstSlot, kArmorFirstSlot + 1) == TransferResult::Rejected);
    CHECK(inventory.slot(kArmorFirstSlot).item == world.head);
    CHECK(inventory.move_slot(kArmorFirstSlot, 1) == TransferResult::Ok);
    CHECK(inventory.slot(1).item == world.head);
    CHECK(inventory.slot(kArmorFirstSlot).empty());
}

TEST_CASE("swap_slots exchanges two cells and is all or nothing") {
    World world;
    auto &inventory = world.inventory;
    CHECK(world.put(0, world.clod, 30));
    CHECK(world.put(kMainFirstSlot, world.rock, 12));

    // Different items: a plain exchange that changes no totals.
    CHECK(inventory.swap_slots(0, kMainFirstSlot) == TransferResult::Ok);
    CHECK(inventory.slot(0).item == world.rock);
    CHECK(inventory.slot(0).count == 12);
    CHECK(inventory.slot(kMainFirstSlot).item == world.clod);
    CHECK(inventory.slot(kMainFirstSlot).count == 30);
    CHECK(inventory.count_of(world.clod) == 30);
    CHECK(inventory.count_of(world.rock) == 12);

    // Same item: still an exchange, never a merge.
    CHECK(world.put(2, world.clod, 5));
    CHECK(world.put(3, world.clod, 7));
    CHECK(inventory.swap_slots(2, 3) == TransferResult::Ok);
    CHECK(inventory.slot(2).count == 7);
    CHECK(inventory.slot(3).count == 5);

    // Across sections: storage <-> offhand.
    CHECK(inventory.swap_slots(kMainFirstSlot, kOffhandFirstSlot) == TransferResult::Ok);
    CHECK(inventory.slot(kOffhandFirstSlot).item == world.clod);
    CHECK(inventory.slot(kMainFirstSlot).empty());

    // An armour cell only takes its own piece, in either direction, so the
    // whole swap is refused and neither cell changes.
    CHECK(world.put(kArmorFirstSlot, world.head, 1));
    CHECK(inventory.swap_slots(kArmorFirstSlot, 0) == TransferResult::Rejected);
    CHECK(inventory.slot(kArmorFirstSlot).item == world.head);
    CHECK(inventory.slot(0).item == world.rock);
    CHECK(inventory.swap_slots(kArmorFirstSlot, kArmorFirstSlot + 1) == TransferResult::Rejected);
    CHECK(inventory.slot(kArmorFirstSlot).item == world.head);
    CHECK(inventory.swap_slots(kArmorFirstSlot, kOffhandFirstSlot) == TransferResult::Rejected);
    CHECK(inventory.slot(kArmorFirstSlot).item == world.head);
    CHECK(inventory.slot(kOffhandFirstSlot).item == world.clod);

    // Into an empty storage cell both directions are legal.
    CHECK(inventory.swap_slots(kArmorFirstSlot, kMainFirstSlot) == TransferResult::Ok);
    CHECK(inventory.slot(kMainFirstSlot).item == world.head);
    CHECK(inventory.slot(kArmorFirstSlot).empty());

    // Degenerate arguments.
    CHECK(inventory.swap_slots(0, 0) == TransferResult::NoChange);
    CHECK(inventory.swap_slots(kArmorFirstSlot + 1, kArmorFirstSlot + 2) == TransferResult::NoChange); // both empty
    CHECK(inventory.swap_slots(-1, 0) == TransferResult::Rejected);
    CHECK(inventory.swap_slots(0, kInventorySlots) == TransferResult::Rejected);
}

TEST_CASE("remove_item drains cells in slot order and reports what it removed") {
    World world;
    auto &inventory = world.inventory;
    CHECK(world.put(0, world.clod, 64));
    CHECK(world.put(1, world.clod, 20));
    CHECK(world.put(5, world.clod, 5));
    CHECK(world.put(9, world.rock, 10));

    CHECK(inventory.remove_item(world.clod, 70) == 70);
    CHECK(inventory.slot(0).empty());     // drained first
    CHECK(inventory.slot(1).count == 14); // 64 plus 6 of the 20
    CHECK(inventory.slot(5).count == 5);  // untouched
    CHECK(inventory.count_of(world.clod) == 19);

    // Removing more than there is stops at the item and reports the truth.
    CHECK(inventory.remove_item(world.clod, 1000) == 19);
    CHECK(inventory.slot(1).empty());
    CHECK(inventory.slot(5).empty());
    CHECK(inventory.count_of(world.clod) == 0);
    CHECK_FALSE(inventory.is_empty()); // the rock is still there

    // Non-positive counts and the empty id remove nothing.
    CHECK(inventory.remove_item(world.rock, 0) == 0);
    CHECK(inventory.remove_item(world.rock, -3) == 0);
    CHECK(inventory.remove_item(ItemRegistry::kEmptyId, 5) == 0);
    CHECK(inventory.count_of(world.rock) == 10);

    CHECK(inventory.remove_from_slot(9, 4) == 4);
    CHECK(inventory.slot(9).count == 6);
    CHECK(inventory.remove_from_slot(9, 100) == 6);
    CHECK(inventory.slot(9).empty());
    CHECK(inventory.remove_from_slot(kInventorySlots, 1) == 0);
    CHECK(inventory.is_empty());
}

TEST_CASE("set_slot writes exactly and refuses an over-limit stack") {
    World world;
    auto &inventory = world.inventory;
    CHECK(inventory.set_slot(0, world.stack(world.clod, kStackLimitLarge)));
    CHECK(inventory.slot(0).count == kStackLimitLarge);

    // Over the cap: refused, never silently clamped.
    CHECK_FALSE(inventory.set_slot(0, world.stack(world.clod, kStackLimitLarge + 1)));
    CHECK(inventory.slot(0).count == kStackLimitLarge);
    CHECK_FALSE(inventory.set_slot(0, world.stack(world.vessel, kStackLimitMedium + 1)));
    CHECK_FALSE(inventory.set_slot(kInventorySlots, world.stack(world.clod, 1)));
    CHECK_FALSE(inventory.set_slot(-1, world.stack(world.clod, 1)));

    // An empty stack clears a cell, whatever the slot type is.
    CHECK(inventory.set_slot(0, ItemStack{}));
    CHECK(inventory.slot(0).empty());
    CHECK(inventory.set_slot(kArmorFirstSlot, ItemStack{}));

    // A hand-built stack with count 0 is stored as the empty stack.
    const ItemStack degenerate{world.clod, 0};
    CHECK(inventory.set_slot(3, degenerate));
    CHECK(inventory.slot(3).empty());
    CHECK(inventory.slot(3) == ItemStack{});
    CHECK(inventory.used_slots() == 0);
}

TEST_CASE("inventories compare by their cells") {
    World world;
    World other;
    CHECK(world.inventory == other.inventory);

    CHECK(world.add(world.clod, 5) == AddResult{5, 0});
    CHECK(world.inventory != other.inventory);

    // Same items in the same cells: equal, even though each world owns its
    // own registry instance.
    CHECK(other.add(other.clod, 5) == AddResult{5, 0});
    CHECK(world.inventory == other.inventory);

    // Items are compared by identity, not by count alone: the same total of
    // a different item is not the same inventory.
    World third;
    CHECK(third.add(third.rock, 5) == AddResult{5, 0});
    CHECK(world.inventory != third.inventory);
}

TEST_CASE("add_to_slot merges into a single cell and reports the remainder") {
    World world;
    auto &inventory = world.inventory;
    CHECK(world.put(4, world.clod, 10));

    ItemStack incoming = world.stack(world.clod, 100);
    CHECK(inventory.add_to_slot(4, incoming) == AddResult{54, 46});
    CHECK(inventory.slot(4).count == kStackLimitLarge);
    CHECK(incoming.count == 46);

    // An empty cell takes the item; a cell of another item takes nothing.
    CHECK(inventory.add_to_slot(5, incoming) == AddResult{46, 0});
    CHECK(inventory.slot(5).item == world.clod);
    CHECK(incoming.empty());

    ItemStack other = world.stack(world.rock, 3);
    CHECK(inventory.add_to_slot(5, other) == AddResult{0, 3});
    CHECK(other.count == 3);
    CHECK(inventory.slot(5).count == 46);
    CHECK(inventory.add_to_slot(kArmorFirstSlot, other) == AddResult{0, 3});
    CHECK(inventory.add_to_slot(kInventorySlots, other) == AddResult{0, 3});
    ItemStack nothing;
    CHECK(inventory.add_to_slot(0, nothing) == AddResult{0, 0});
}
