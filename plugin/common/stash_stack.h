// The RotW stash tabs (Gems, Materials, Runes) with PD2's stacks (#1537), with no engine types so
// tests/stash_stack_check.cpp builds it on the host. A tab slot keeps a count byte (at most 99) that D2R raises by one per
// item put in; a PD2 stack is `quantity` items (stat 70; a single may have none), so it raises the count by that many.
#pragma once

#include <cstdint>

namespace pd2::stashstack {

// D2R's cap: the deposit refuses when the slot's count is not below it. Not raised to 255 (#1763): D2RCore's add (the
// .maho thunk stash_stacks.cpp wraps) refuses past 99 itself, so a patched check let a deposit lose the items past 99 (Slot5).
inline constexpr int32_t TabMax = 99;

// How many items a stack of `quantity` is
constexpr auto Units(int32_t quantity) noexcept -> int32_t {
	return quantity < 1 ? 1 : quantity;
}

// The count D2R's "tab full" check (count < 99) is shown, so the whole stack must fit: count + units <= 99
constexpr auto CheckedCount(uint8_t count, int32_t quantity) noexcept -> uint8_t {
	const int32_t seen = count + Units(quantity) - 1;
	return static_cast<uint8_t>(seen > 255 ? 255 : seen);
}

}  // namespace pd2::stashstack
