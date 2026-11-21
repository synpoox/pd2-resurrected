// Item Quantity (#1596): how many extra rolls of a monster's own TC a kill gets, without engine types.
#pragma once

#include <cstdint>

namespace pd2::quantity {

inline constexpr int32_t MaxExtraRolls = 10;  // a cap, so a huge stat cannot stall the server thread

// `percent` is the killer's Item Quantity stat; each 100 is one extra roll, the rest a percent chance of one more.
// `roll` is a random number in 0..99.
constexpr auto ExtraRolls(int32_t percent, uint32_t roll) noexcept -> int32_t {
	if (percent <= 0) {
		return 0;
	}
	const int32_t n = percent / 100 + (roll < static_cast<uint32_t>(percent % 100) ? 1 : 0);
	return n < MaxExtraRolls ? n : MaxExtraRolls;
}

static_assert(ExtraRolls(0, 0) == 0 && ExtraRolls(-30, 0) == 0);
static_assert(ExtraRolls(30, 29) == 1 && ExtraRolls(30, 30) == 0 && ExtraRolls(30, 99) == 0);
static_assert(ExtraRolls(100, 99) == 1 && ExtraRolls(130, 0) == 2 && ExtraRolls(130, 30) == 1);
static_assert(ExtraRolls(5000, 0) == MaxExtraRolls);

}  // namespace pd2::quantity
