// What the map opening and the uber entry share (open_gate.cpp, frozen): the game counters, the player's level and
// the portal maker both open portals with. Split out of map_open.cpp (#943).
#pragma once

#include "pd2_engine.h"

#include <atomic>
#include <cstdint>

// The game counter key of map (and, with UB2, uber area) openings: PD2 allows one per game
inline constexpr uint32_t OpenKey = 0x4d415053;  // 'MAPS'

// Opening attempts D2R or the warp refused: one count for maps and uber areas, as in the status line.
extern std::atomic<uint32_t> g_openFailed;

// The game's counter for `key`, inserted at 0 when missing (it may move the others: fetch it each time).
auto OpenCounter(d2r::Game* game, uint32_t key) noexcept -> int32_t*;
// The level the player stands in; -1 when no room.
auto PlayerLevelId(d2r::Unit* player) noexcept -> int32_t;
// Opens a portal object (objects row `row`, operate 60) at (x, y) to `level`; `near` is a room by the spot. Null and a
// warning when none is made.
auto OpenPortalAt(d2r::Game* game, void* near, int32_t row, int32_t x, int32_t y, int32_t level) noexcept -> d2r::Unit*;

// The argument block D2R's operate dispatcher 0x592c40 passes an operate function (rva::ObjectOperateTable)
struct ObjectOperate {
	d2r::Game* game;
	d2r::Unit* object;
	d2r::Unit* player;
	uint64_t seed;
	int32_t classId;
};
