// The RotW stash tabs (Gems, Materials, Runes) with PD2's stacks (#1537), with no engine types so
// tests/stash_stack_check.cpp builds it on the host. A tab slot keeps a count byte (D2R: at most 99, raised to 255 by #1763) that D2R raises by one per
// item put in; a PD2 stack is `quantity` items (stat 70; a single may have none), so it raises the count by that many.
#pragma once

#include <cstdint>

namespace pd2::stashstack {

// The deposit refuses when the slot's count is not below it. D2R's 99 is patched to 255 (#1763), the 8-bit saved count's
// ceiling: the item writer (93847 0x37fbec) saves min(count, 0xff) in 8 bits, so 255 never wraps.
inline constexpr int32_t TabMax = 255;

// How many items a stack of `quantity` is
constexpr auto Units(int32_t quantity) noexcept -> int32_t {
	return quantity < 1 ? 1 : quantity;
}

// The count D2R's "tab full" check (count < TabMax) is shown, so the whole stack must fit: count + units <= TabMax
constexpr auto CheckedCount(uint8_t count, int32_t quantity) noexcept -> uint8_t {
	const int32_t seen = count + Units(quantity) - 1;
	return static_cast<uint8_t>(seen > 255 ? 255 : seen);
}

static_assert(TabMax == 255 && CheckedCount(205, 50) < TabMax && CheckedCount(206, 50) >= TabMax);  // 255 fits, 256 not

}  // namespace pd2::stashstack
