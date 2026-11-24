// PD2's kick base (#2051), shared by the server kick (core-rules skill/kick_base.cpp) and the skill tooltip's kick line
// (core-ui descdam_lines.cpp, descdam 15): one formula, one set of engine signatures.
#pragma once

#include "sig.h"

#include <climits>
#include <cstdint>
#include <cstring>

namespace kick_base {

// PD2's base before the roll, in 1/256 (0x103022c0's sums and 0x1037c530's scaling)
struct KickStats {
	int32_t minDam, maxDam;    // the unit's 21 / 22
	int32_t weapMin, weapMax;  // the weapon's own 21 / 22
	int32_t normal;            // item_normaldamage (111)
	int32_t dex;
	int32_t bootsMin, bootsMax;  // the boots helper's sums
	int32_t pct;                 // 100 + damage% + the boots' + undead / demon
};
struct KickRange {
	int32_t min, max;
};

constexpr auto Scale(int32_t v, int32_t pct) noexcept -> int32_t {
	const double r = static_cast<double>(v) * pct / 100.0;
	return r > 2147483647.0 ? INT_MAX : static_cast<int32_t>(r);
}

constexpr auto Pd2Base(const KickStats& s) noexcept -> KickRange {
	const int32_t min = (s.minDam - s.weapMin + s.normal) * 256 + s.bootsMin * 256 + s.dex * 256 / 4;
	const int32_t max = (s.maxDam - s.weapMax + s.normal) * 256 + s.bootsMax * 256 + s.dex * 256 / 3;
	return {Scale(min, s.pct), Scale(max, s.pct)};
}

// 10-20 on the unit with a 5-10 weapon, 3 normal, 90 dex, 20-40 boots, pct 100 + 60 + 0
static_assert(Pd2Base({10, 20, 5, 10, 3, 90, 20, 40, 160}).min == ((8 * 256 + 20 * 256 + 5760) * 16) / 10);
static_assert(Pd2Base({10, 20, 5, 10, 3, 90, 20, 40, 160}).max == (13 * 256 + 40 * 256 + 7680) * 16 / 10);
static_assert(Pd2Base({0, 0, 0, 0, 0, 0, 0, 0, 100}).max == 0);
static_assert(Pd2Base({0, 0, 0, 0, 0, 1, 0, 0, 100}).min == 64);  // dex x 256 / 4, then / 3 for the max
static_assert(Pd2Base({INT_MAX / 512, INT_MAX / 512, 0, 0, 0, 0, 0, 0, 100000}).max == INT_MAX);

// ... and its weapon block 0x573f53: the wielded-weapon getter (inventory, E8 at +15, 0x387690, PD2's 0x1030ba90) and
// the usable test (E8 at +79, 0x36ad20, PD2's 0x10308160: 0 for a broken item). Not UnitWeapon: it follows the
// current skill's weapsel, none for a kick, while the unit's 21/22 still hold the weapon's
constexpr const char* WeaponBlock =
    "48 8b e8 48 85 c0 0f 84 ?? ?? ?? ?? 48 8b c8 e8 ?? ?? ?? ?? 33 ff 48 8b d8 48 85 c0 74 ?? 48 8b c8 e8 ?? ?? ?? ?? "
    "33 d2 48 8b cd 3c 04 0f 94 c2 83 c2 04 e8 ?? ?? ?? ?? 48 8b f8 ba 2d 00 00 00 48 8b cb e8 ?? ?? ?? ?? 85 c0 74 ?? "
    "48 8b cb e8";
constexpr uint32_t WeaponCallAt = 15, UsableCallAt = 79;

constexpr int32_t StatDex = 2, StatMin = 21, StatMax = 22, StatNormal = 111, StatDemon = 121, StatUndead = 122;
constexpr int32_t TypeWeapon = 45;

// The target of the E8 call at `callAt` in the unique match of `pattern` (0 when not unique)
inline auto CallTarget(uint64_t exeBase, const char* pattern, uint32_t callAt) noexcept -> uint64_t {
	uint64_t rva = 0;
	if (sigscan::ScanUnique(exeBase, pattern, callAt, &rva) != 1) {
		return 0;
	}
	int32_t rel = 0;
	std::memcpy(&rel, reinterpret_cast<const void*>(exeBase + rva + 1), sizeof(rel));
	return exeBase + rva + 5 + static_cast<int64_t>(rel);
}
}  // namespace kick_base
