// Multiple Shot's missile line without engine types, so a host check covers it (plugin/tests/line_spread_check.cpp).
// PD2's srvdo 8 (#1056) and D2R's srvdo 8 (0x557840) both fly `count` missiles from the caster to points one small
// step apart on a line through the target, at right angles to the aim. The middle `triggered` ones trigger hits, the
// rest are NotTriggering. triggered 0 means none trigger (PD2); above count it means all (clamped to count).
#pragma once

#include <cstdint>

namespace pd2::line_spread {

// The two vector steps are D2Common's 0x3376d0 (scale a short vector up) and 0x337750 (turn it 90 degrees and shrink
// it to length^2 <= 3). create(x, y, outer) is called once per missile and returns true if the missile was made.
template <typename Create>
auto Spread(int32_t ux, int32_t uy, int32_t tx, int32_t ty, int32_t count, int32_t triggered, Create create)
	noexcept -> int32_t {
	int32_t dx = tx - ux, dy = ty - uy;
	if (dx * dx + dy * dy < 4) {
		dx *= 4;
		dy *= 4;
	}
	if (dx * dx + dy * dy < 16) {
		dx *= 2;
		dy *= 2;
	}
	int32_t sx = dy, sy = -dx;
	while (sx * sx + sy * sy > 3) {
		sx /= 2;
		sy /= 2;
	}
	tx -= sx * count / 2;
	ty -= sy * count / 2;
	if (triggered < 0) {
		triggered = 0;
	} else if (triggered > count) {
		triggered = count;
	}
	const int32_t outer = (count - triggered) / 2;
	int32_t created = 0;
	for (int32_t i = 0; i < count; ++i, tx += sx, ty += sy) {
		created += create(tx, ty, i < outer || i >= outer + triggered) ? 1 : 0;
	}
	return created;
}

}  // namespace pd2::line_spread
