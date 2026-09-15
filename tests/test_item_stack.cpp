#include <cstdint>
#include <stdexcept>

#include <doctest/doctest.h>

#include "opencraft/game/item_registry.hpp"
#include "opencraft/game/item_stack.hpp"

namespace {

using opencraft::game::ItemRegistry;
using opencraft::game::ItemStack;
using opencraft::game::kStackLimitLarge;
using opencraft::game::kStackLimitMedium;
using opencraft::game::kStackLimitSingle;
using opencraft::game::stack_limit_of;

struct Items {
    ItemRegistry registry = ItemRegistry::create_default();
    std::uint16_t clod = registry.id_of("loam_clod");      // stacks to 64
    std::uint16_t rock = registry.id_of("greyrock");       // stacks to 64
    std::uint16_t vessel = registry.id_of("empty_vessel"); // stacks to 16
    std::uint16_t tool = registry.id_of("timber_chisel");  // stacks to 1
};

} // namespace

TEST_CASE("an item stack is empty by id or by count") {
    const Items items;
    CHECK(ItemStack{}.empty());
    CHECK(ItemStack::of(ItemRegistry::kEmptyId, 7).empty());
    CHECK(ItemStack::of(items.clod, 0).empty());
    CHECK(ItemStack::of(items.clod, -2).empty());
    CHECK_FALSE(ItemStack::of(items.clod, 1).empty());

    // Normalisation: an empty id or a non-positive count yields the default
    // stack, so equal contents always compare equal.
    CHECK(ItemStack::of(ItemRegistry::kEmptyId, 7) == ItemStack{});
    CHECK(ItemStack::of(items.clod, 0) == ItemStack{});
    CHECK(ItemStack::of(items.clod, 3).item == items.clod);
    CHECK(ItemStack::of(items.clod, 3).count == 3);
}

TEST_CASE("two stacks merge only when they name the same non-empty item") {
    const Items items;
    const ItemStack clod_ten = ItemStack::of(items.clod, 10);
    const ItemStack clod_five = ItemStack::of(items.clod, 5);
    const ItemStack rock = ItemStack::of(items.rock, 5);

    CHECK(clod_ten.same_item(clod_five));
    CHECK(ItemStack::can_merge(clod_ten, clod_five));
    CHECK_FALSE(clod_ten.same_item(rock));
    CHECK_FALSE(ItemStack::can_merge(clod_ten, rock));
    // An empty stack never merges: it carries no item identity.
    CHECK_FALSE(clod_ten.same_item(ItemStack{}));
    CHECK_FALSE(ItemStack{}.same_item(clod_ten));
    CHECK_FALSE(ItemStack{}.same_item(ItemStack{}));
}

TEST_CASE("merge_from fills up to the limit and drains the source") {
    const Items items;
    ItemStack target = ItemStack::of(items.clod, 60);
    ItemStack source = ItemStack::of(items.clod, 10);

    CHECK(target.merge_from(source, kStackLimitLarge) == 4);
    CHECK(target.count == kStackLimitLarge);
    CHECK(source.count == 6);

    // A full target takes nothing.
    CHECK(target.merge_from(source, kStackLimitLarge) == 0);
    CHECK(source.count == 6);

    // Draining empties and normalises the source.
    CHECK(target.merge_from(source, kStackLimitLarge + 6) == 6);
    CHECK(source.empty());
    CHECK(source == ItemStack{});

    // A different item never merges.
    ItemStack other = ItemStack::of(items.rock, 5);
    CHECK(target.merge_from(other, kStackLimitLarge) == 0);
    CHECK(other.count == 5);
}

TEST_CASE("merge_from fills an empty stack by adopting its item") {
    const Items items;
    ItemStack empty;
    ItemStack source = ItemStack::of(items.rock, 30);
    CHECK(empty.merge_from(source, kStackLimitLarge) == 30);
    CHECK(empty.item == items.rock);
    CHECK(empty.count == 30);
    CHECK(source.empty());

    // The limit, not the source, decides how much fits.
    ItemStack medium_cell;
    ItemStack vessels = ItemStack::of(items.vessel, 20);
    CHECK(medium_cell.merge_from(vessels, kStackLimitMedium) == kStackLimitMedium);
    CHECK(medium_cell.count == kStackLimitMedium);
    CHECK(vessels.count == 4);

    // A non-positive limit takes nothing.
    ItemStack zero_limit;
    ItemStack clod = ItemStack::of(items.clod, 3);
    CHECK(zero_limit.merge_from(clod, 0) == 0);
    CHECK(zero_limit.empty());
    CHECK(clod.count == 3);
}

TEST_CASE("split and shrink remove units and normalise what is left") {
    const Items items;
    ItemStack stack = ItemStack::of(items.clod, 64);

    const ItemStack taken = stack.split(20);
    CHECK(taken.item == items.clod);
    CHECK(taken.count == 20);
    CHECK(stack.count == 44);

    // Taking more than there is yields the whole stack and empties the source.
    const ItemStack rest = stack.split(100);
    CHECK(rest.count == 44);
    CHECK(stack.empty());
    CHECK(stack == ItemStack{});

    // A non-positive amount takes nothing and returns nothing.
    ItemStack again = ItemStack::of(items.clod, 5);
    CHECK(again.split(0).empty());
    CHECK(again.split(-1).empty());
    CHECK(again.count == 5);

    CHECK(again.shrink(2) == 2);
    CHECK(again.count == 3);
    CHECK(again.shrink(100) == 3);
    CHECK(again.empty());
    CHECK(again.shrink(1) == 0);
    CHECK(ItemStack{}.shrink(4) == 0);
    CHECK(ItemStack{}.split(4).empty());

    const ItemStack single = ItemStack::of(items.tool, 1);
    CHECK(single.stack_limit(items.registry) == kStackLimitSingle);
}

TEST_CASE("stack limits come from the registry and the empty id has none") {
    const Items items;
    CHECK(ItemStack::of(items.clod, 1).stack_limit(items.registry) == kStackLimitLarge);
    CHECK(ItemStack::of(items.vessel, 1).stack_limit(items.registry) == kStackLimitMedium);
    CHECK(ItemStack::of(items.tool, 1).stack_limit(items.registry) == kStackLimitSingle);
    CHECK(stack_limit_of(items.registry, ItemRegistry::kEmptyId) == 0);
    CHECK(ItemStack{}.stack_limit(items.registry) == 0);
    // An unknown numeric id is reported the same way ItemRegistry::def_of
    // reports it.
    CHECK_THROWS_AS([&] { static_cast<void>(stack_limit_of(items.registry, 0xFFFF)); }(), std::out_of_range);
}
