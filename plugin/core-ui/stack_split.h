// PD2's stack split (#316): with a stack of N > 1 held, Ctrl+left-click puts one item down and keeps N-1 on the cursor.
// D2R puts the whole stack down as usual (or merges it into a stack of the same item, #362); then the SDK's splitStack
// takes N-1 of it back to the cursor.
#pragma once

#include <D2RLPlugin/api.h>
#include <D2RLPlugin/item.h>

#include <cstddef>
#include <cstdint>

namespace stack_split {

// The grids a split can land in: the inventory, cube, both stashes and a custom page
inline constexpr uint32_t StoredGrids =
	D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Inventory) | D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Cube) |
	D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::PersonalStash) |
	D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::SharedStash) | D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::CustomPage);

// A held stack the pad legend and a split apply to: more than one item, and not a tome (PD2 never splits tomes, whose
// quantity is their scrolls)
// shortcut: the pad legend still shows for a throwable or ammo (the type test needs the game thread); upgrade if pad
// players ask.
constexpr auto Splittable(uint32_t code, int32_t held) noexcept -> bool {
	return held > 1 && code != D2RL::Items::MakeItemCode("tbk") && code != D2RL::Items::MakeItemCode("ibk");
}

// PD2 never splits a throwable (itemtypes `Throwable`: `thro` and the types under it, throwing knives, axes, javelins
// and potions) or ammo (`misl`: arrows and bolts) (#1789): ProjectDiablo.dll 0x1037de30 tests D2Common #10711 and type
// 0x38. Without it a bought stack splits into stacks of 1 that each sell at the price of the whole stack.
inline constexpr int32_t TypeThrown = 48, TypeMissile = 56;  // itemtypes rows thro, misl (as ItemIsType numbers them)

template <class IsType>
constexpr auto RefusedByType(IsType isType) noexcept -> bool {
	return isType(TypeThrown) || isType(TypeMissile);
}

// How many items go back to the cursor: N-1 when a stack of `held` left the cursor with Ctrl down and the stack it
// landed in, in a stored grid, rose from `before` to `placed` by all of it: the held stack itself, whole (`before` 0),
// or a stack of the same item it merged into (PD2 adds one to that stack). Otherwise 0 (a plain put-down, the ground,
// the belt, a single item, a tome: PD2 never splits tomes, whose quantity is their scrolls).
constexpr auto SplitOff(uint32_t code, int32_t held, bool ctrl, int32_t before, int32_t placed,
                        D2RL::Items::ItemContainer where) noexcept -> uint32_t {
	const bool stored = (D2RL::Items::ContainerBit(where) & StoredGrids) != 0;
	return ctrl && Splittable(code, held) && placed == before + held && stored ? static_cast<uint32_t>(held - 1) : 0;
}

// Starts the cursor watch on the UI thread. Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// The split counters and the watch's own, for the status command.
void Counters(char* out, size_t size) noexcept;

}  // namespace stack_split
