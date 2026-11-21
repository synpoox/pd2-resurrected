// The shrine rules without engine types, so a host check covers them (plugin/tests/shrine_rules_check.cpp, #1143).
// PD2's shrine installer (docs/research/w37-server-rules.md) shares the booster shrines, codes 6-15, with every player
// whose position is within a squared distance of 450 of the user, and adds +10 to the matching max resist after the
// four resist shrines, codes 8-11.
#pragma once

#include <cstdint>

namespace pd2::shrines {

inline constexpr int32_t ShareDistanceSq = 450;
inline constexpr int FirstBooster = 6, StaminaShrine = 14, LastBooster = 15, GemShrine = 18;
inline constexpr int32_t MaxResistBonus = 10;
inline constexpr int32_t StaminaVelocityBonus = 35;

inline constexpr auto IsBooster(int code) noexcept -> bool { return code >= FirstBooster && code <= LastBooster; }

// Whether another player at (x, y) is inside the shared distance of the user at (ux, uy)
inline constexpr auto InShareRange(int32_t ux, int32_t uy, int32_t x, int32_t y) noexcept -> bool {
	const int64_t dx = x - ux, dy = y - uy;
	return dx * dx + dy * dy < ShareDistanceSq;
}

// itemstatcost id of the max resist a resist shrine raises (8 fire, 9 cold, 10 lightning, 11 poison), 0 for any other code
inline constexpr auto MaxResistStat(int code) noexcept -> int32_t {
	switch (code) {
		case 8: return 40;   // maxfireresist
		case 9: return 44;   // maxcoldresist
		case 10: return 42;  // maxlightresist
		case 11: return 46;  // maxpoisonresist
		default: return 0;
	}
}

}  // namespace pd2::shrines
