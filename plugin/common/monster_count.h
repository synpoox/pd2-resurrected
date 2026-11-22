// Monsters remaining (#1534, #1656): the numbers behind the zone info line, with no engine types so a host check can run them.
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

constexpr int32_t MaxLevel = 1024;  // upper bound for any caller's own cap; Read takes the levels table's row count
constexpr int32_t MaxCount = 100000;  // a larger value is not a ledger (a stale or wrong pointer)

// region[level] -> ledger. `regions` is D2R's pointer array indexed by level id; `spawnedAt` / `killedAt` are the byte
// offsets of the two ints in a region (build 93847: 0x2cc, 0x2d0); `levelCount` is the levels table row count. False for a null region, a bad level or a value that
// is no count.
inline auto Read(const uint8_t* const* regions, int32_t level, int32_t levelCount, size_t spawnedAt, size_t killedAt,
                 Ledger* out) noexcept -> bool {
	if (regions == nullptr || level < 0 || level >= levelCount || level >= MaxLevel) {
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

// A level that counted no monster (an empty area) shows nothing; a cleared one shows 0. Towns are IsTown's: their
// ledger is not empty (the Rogue Encampment read spawned 24).
constexpr auto Shown(Ledger l) noexcept -> bool {
	return l.spawned > 0;
}

// The five town levels, the list PD2 hard-codes (ProjectDiablo.dll, docs/research/monsters-remaining.md): D2R's levels.txt
// has no town column and the SDK no town query. MonDen 0 would also hide the Ancients, Baal's chamber and Uber Tristram.
constexpr auto IsTown(int32_t level) noexcept -> bool {
	return level == 1 || level == 40 || level == 75 || level == 103 || level == 109;
}

// A level's rooms as its region counts them: those that have made their monsters, and all it counts (PD2's region
// +0x04 and +0x0c; D2R 93847 keeps both at the same offsets, +0x0c is -1 until set). Not the drlg room list: an outdoor
// level's list holds rooms that never make monsters (Blood Moor: 84 listed, 47 counted), so 90% of it is never reached (#1729).
struct Rooms {
	int32_t total = 0;
	int32_t made = 0;
};

constexpr int32_t MaxRooms = 32767;  // a larger value is not a room count

inline auto ReadRooms(const uint8_t* region, size_t madeAt, size_t totalAt) noexcept -> Rooms {
	Rooms r;
	std::memcpy(&r.made, region + madeAt, sizeof r.made);
	std::memcpy(&r.total, region + totalAt, sizeof r.total);
	return r.total > 0 && r.total <= MaxRooms && r.made >= 0 && r.made <= MaxRooms ? r : Rooms {};
}

// PD2's rule (ProjectDiablo.dll 0x103453f0): the count is final once rooms made x 100 / rooms >= 90 (integer division)
constexpr int32_t ExploredPercent = 90;
constexpr auto Explored(Rooms r) noexcept -> bool {
	return r.total > 0 && r.made * 100 / r.total >= ExploredPercent;
}

// The owner's rule (#1656, #1729: 50): "50+" over Cap or while the level is not explored (PD2's "100+"), else the plain number
constexpr int32_t Cap = 50;
inline auto Text(int32_t remaining, bool explored, char* out, size_t size) noexcept -> int {
	return !explored || remaining > Cap ? std::snprintf(out, size, "Monsters Remaining: %d+", Cap)
	                                    : std::snprintf(out, size, "Monsters Remaining: %d", remaining);
}

}  // namespace pd2::monstercount
