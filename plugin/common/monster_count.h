// Monsters remaining (#1534, #1656): the numbers behind the zone info line, with no engine types so a host check can run them.
// D2R keeps a ledger per level (MonsterRegion.cpp, the one its Den of Evil check reads): counted hostile monsters
// spawned and killed. Pets, summons, mercenaries, NPCs and monstats rows flagged neverCount never enter it.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

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

// Rooms seen populated, one set per level id: D2R frees a room's data once the player leaves, so a level left and
// re-entered (a town portal) keeps what it had. Cleared only on a new game.
struct SeenRooms {
	std::unordered_map<int32_t, std::unordered_set<const void*>> byLevel;
	auto Has(int32_t level, const void* room) const -> bool {
		const auto it = byLevel.find(level);
		return it != byLevel.end() && it->second.count(room) != 0;
	}
	void Add(int32_t level, const void* room) { byLevel[level].insert(room); }
};

// A level's rooms: all of them, and those that have made their monsters (PD2's region +0x0c and +0x04)
struct Rooms {
	int32_t total = 0;
	int32_t made = 0;
};

constexpr int32_t MaxRooms = 32767;  // a room list longer than this is not a list (a loop or a wrong pointer)

// Walks the level's room list (`first`, next room pointer at `nextAt`); `populated(room)` says whether that room has
// made its monsters. Every room is visited, so populated() can remember rooms as it goes. A list past MaxRooms is none.
template <class Populated>
inline auto CountRooms(const uint8_t* first, size_t nextAt, Populated&& populated) noexcept -> Rooms {
	Rooms rooms;
	for (const uint8_t* r = first; r != nullptr && rooms.total < MaxRooms; ++rooms.total) {
		rooms.made += populated(r) ? 1 : 0;
		std::memcpy(&r, r + nextAt, sizeof r);
	}
	return rooms.total < MaxRooms ? rooms : Rooms {};
}

// PD2's rule (ProjectDiablo.dll 0x103453f0): the count is final once rooms made x 100 / rooms >= 90 (integer division)
constexpr int32_t ExploredPercent = 90;
constexpr auto Explored(Rooms r) noexcept -> bool {
	return r.total > 0 && r.made * 100 / r.total >= ExploredPercent;
}

// The owner's rule (#1656): "25+" over Cap or while the level is not explored (PD2's "100+"), else the plain number
constexpr int32_t Cap = 25;
inline auto Text(int32_t remaining, bool explored, char* out, size_t size) noexcept -> int {
	return !explored || remaining > Cap ? std::snprintf(out, size, "Monsters Remaining: %d+", Cap)
	                                    : std::snprintf(out, size, "Monsters Remaining: %d", remaining);
}

}  // namespace pd2::monstercount
