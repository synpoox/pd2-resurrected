// The map tooltip's "Immunities: C130, F125" line (#1552): the monster resists of a map that reach 100 or more, each
// entry in its element's colour. Pure text building, no engine types. Colours are PD2's own for the elements (its
// "Enemy fire resistances:" strings use fire c1, cold c3, lightning c9, poison c2); letters C F L P, in that order.
// Maps carry no physical or magic resist stat, so those have no letter.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace pd2::mapimm {

// Cartographer's merged itemstatcost ids (files/data/global/excel/itemstatcost.txt, cartographer_mon_*_res).
inline constexpr uint32_t StatFire = 567, StatCold = 568, StatLight = 569, StatPoison = 570;

// The start of D2R's inline colour marker; the colour digit follows.
inline constexpr const char* ColorMarker = "\xC3\xBF" "c";

struct Resists {
	int32_t fire = 0, cold = 0, light = 0, poison = 0;
};

// Writes the line into `out` (NUL ended) and returns its length; 0 and an empty string when no resist is 100 or more
// or the buffer is too small for the whole line.
inline auto Line(const Resists& r, char* out, size_t capacity) noexcept -> size_t {
	if (capacity == 0) {
		return 0;
	}
	out[0] = '\0';
	const struct {
		char letter, color;
		int32_t value;
	} entries[] = {{'C', '3', r.cold}, {'F', '1', r.fire}, {'L', '9', r.light}, {'P', '2', r.poison}};
	size_t len = 0;
	bool any = false;
	for (const auto& e : entries) {
		if (e.value < 100) {
			continue;
		}
		const int n = std::snprintf(out + len, capacity - len, "%s%s%c%c%d%s0", any ? ", " : "Immunities: ", ColorMarker, e.color, e.letter, e.value, ColorMarker);
		if (n < 0 || static_cast<size_t>(n) >= capacity - len) {
			out[0] = '\0';
			return 0;
		}
		len += static_cast<size_t>(n);
		any = true;
	}
	return len;
}

}  // namespace pd2::mapimm
