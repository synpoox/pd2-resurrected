// Monsters remaining (#1534): the numbers behind the zone info line, with no engine types so a host check can run them.
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
constexpr int32_t MaxRooms = 32767;  // a room list longer than this is not a list (a loop or a wrong pointer)
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

// A level that counted no monster (a town, an empty area) shows nothing; a cleared one shows 0.
constexpr auto Shown(Ledger l) noexcept -> bool {
	return l.spawned > 0;
}

// Rooms seen populated, one set per level id: D2R frees a room's data once the player leaves, so a level
// left and re-entered (a town portal) must keep what it had. Cleared only on a new game.
struct SeenRooms {
	std::unordered_map<int32_t, std::unordered_set<const void*>> byLevel;
	auto Has(int32_t level, const void* room) const -> bool {
		const auto it = byLevel.find(level);
		return it != byLevel.end() && it->second.count(room) != 0;
	}
	void Add(int32_t level, const void* room) { byLevel[level].insert(room); }
	auto Count(int32_t level) const -> size_t {
		const auto it = byLevel.find(level);
		return it == byLevel.end() ? 0 : it->second.size();
	}
};

// D2R makes a room's monsters when the room first activates, so the ledger only knows the rooms met so far. Walk the level's
// room list (`first`, next room pointer at `nextAt`); `populated(room)` says whether that room has made its monsters.
// True only when the list is real (not empty, under MaxRooms) and every room is populated: then the count is exact.
// Every room is visited (no early exit) so a caller's populated() can remember rooms as it goes.
template <class Populated>
inline auto AllPopulated(const uint8_t* first, size_t nextAt, Populated&& populated) noexcept -> bool {
	int32_t rooms = 0;
	bool all = true;
	for (const uint8_t* r = first; r != nullptr && rooms < MaxRooms; ++rooms) {
		all = populated(r) && all;
		std::memcpy(&r, r + nextAt, sizeof r);
	}
	return rooms > 0 && rooms < MaxRooms && all;
}

// "Monsters remaining: 7+" while rooms are still unmet (the count can grow), "7" once every room has made its monsters
inline auto Text(int32_t remaining, bool complete, char* out, size_t size) noexcept -> int {
	return std::snprintf(out, size, "Monsters remaining: %d%s", remaining, complete ? "" : "+");
}

}  // namespace pd2::monstercount
