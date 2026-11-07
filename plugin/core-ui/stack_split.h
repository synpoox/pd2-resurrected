// PD2's stack split (#316): with a stack of N > 1 held, Ctrl+left-click puts one item down and keeps N-1 on the cursor.
// D2R puts the whole stack down as usual; then the SDK's splitStack takes N-1 of it back to the cursor.
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

// How many items go back to the cursor: N-1 when a stack of `held` left the cursor with Ctrl down and lies, whole, in
// a stored grid; otherwise 0 (a plain put-down, a merge into another stack, the ground, the belt, a single item, a tome:
// PD2 never splits tomes, whose quantity is their scrolls).
constexpr auto SplitOff(uint32_t code, int32_t held, bool ctrl, int32_t placed, D2RL::Items::ItemContainer where) noexcept
	-> uint32_t {
	const bool tome = code == D2RL::Items::MakeItemCode("tbk") || code == D2RL::Items::MakeItemCode("ibk");
	const bool stored = (D2RL::Items::ContainerBit(where) & StoredGrids) != 0;
	return ctrl && !tome && held > 1 && placed == held && stored ? static_cast<uint32_t>(held - 1) : 0;
}

// Starts the cursor watch on the UI thread. Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// The split counters and the watch's own, for the status command.
void Counters(char* out, size_t size) noexcept;

}  // namespace stack_split
