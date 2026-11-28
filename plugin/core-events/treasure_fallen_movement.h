// PD2 Treasure Fallen movement state (ProjectDiablo.dll 0x1035a3c0, #2218).
#pragma once

#include <cstdint>

namespace treasure_fallen {

enum class Movement { Flee, Wander, Blink };

inline auto Track(uint32_t& position, int32_t& stationary, int32_t x, int32_t y, int32_t distance) noexcept -> Movement {
	// PD2 compares x*y, rather than storing the two coordinates separately.
	const uint32_t current = static_cast<uint32_t>(x) * static_cast<uint32_t>(y);
	if (current != position) {
		position = current;
		stationary = 0;
		return Movement::Flee;
	}
	if (stationary > 2 && distance < 10) {
		stationary = 0;
		return Movement::Blink;
	}
	if (stationary < INT32_MAX) {
		++stationary;
	}
	return Movement::Wander;
}

inline auto BlinkCoordinate(int32_t own, int32_t target, uint32_t roll) noexcept -> int32_t {
	const int32_t step = 2 + static_cast<int32_t>(roll);
	// PD2's recovery Blink points toward the target on each axis, including +step on equal coordinates.
	return own <= target ? own + step : own - step;
}

}  // namespace treasure_fallen
