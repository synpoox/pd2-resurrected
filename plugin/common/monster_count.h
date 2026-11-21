// Monsters remaining (#1534): the numbers behind the zone info line, with no engine types so a host check can run them.
// D2R keeps a ledger per level (MonsterRegion.cpp, the one its Den of Evil check reads): counted hostile monsters
// spawned and killed. Pets, summons, mercenaries, NPCs and monstats rows flagged neverCount never enter it.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace pd2::monstercount {

struct Ledger {
	int32_t spawned = 0;
	int32_t killed = 0;
};

constexpr int32_t MaxLevel = 1024;  // the region array has no bounds check: no level id past this is read
constexpr int32_t MaxCount = 100000;  // a larger value is not a ledger (a stale or wrong pointer)

// region[level] -> ledger. `regions` is D2R's pointer array indexed by level id; `spawnedAt` / `killedAt` are the byte
// offsets of the two ints in a region (build 93847: 0x2cc, 0x2d0). False for a null region, a bad level or a value that
// is no count.
inline auto Read(const uint8_t* const* regions, int32_t level, size_t spawnedAt, size_t killedAt, Ledger* out) noexcept -> bool {
	if (regions == nullptr || level < 0 || level >= MaxLevel) {
		return false;
	}
	const uint8_t* region = regions[level];
	if (region == nullptr) {
		return false;
	}
	Ledger l;
	std::memcpy(&l.spawned, region + spawnedAt, sizeof l.spawned);
	std::memcpy(&l.killed, region + killedAt, sizeof l.killed);
	if (l.spawned < 0 || l.spawned > MaxCount || l.killed < 0 || l.killed > MaxCount) {
		return false;
	}
	*out = l;
	return true;
}

// Never below 0: a resurrected monster takes its kill back, an ally turned hostile adds a spawn.
constexpr auto Remaining(Ledger l) noexcept -> int32_t {
	return l.spawned > l.killed ? l.spawned - l.killed : 0;
}

// A level that counted no monster (a town, an empty area) shows nothing; a cleared one shows 0.
constexpr auto Shown(Ledger l) noexcept -> bool {
	return l.spawned > 0;
}

inline auto Text(Ledger l, char* out, size_t size) noexcept -> int {
	return std::snprintf(out, size, "Monsters remaining: %d", Remaining(l));
}

}  // namespace pd2::monstercount
