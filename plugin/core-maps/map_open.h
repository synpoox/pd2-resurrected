// Opening a map: item spell 16 (map_open.cpp).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Installs the item spell check and use hooks; `report` gets one entry. Returns how many rules were installed.
auto InstallMapOpen(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto MapOpenCount() noexcept -> int;
// Live counters for the status command.
auto MapOpenCounters(char* out, size_t size) noexcept -> void;
// Opens a portal object (objects row `row`, operate 60) at (x, y) to `level`; `near` is a room by the spot. Null and a
// warning when none is made.
auto OpenPortalAt(d2r::Game* game, void* near, int32_t row, int32_t x, int32_t y, int32_t level) noexcept -> d2r::Unit*;
// True for a waypoint preset in an uber level, which PD2 leaves without one: the preset spawn skips it.
auto NoUberWaypoint(d2r::Game* game, void* room, const uint8_t* preset) noexcept -> bool;

// The argument block D2R's operate dispatcher 0x592c40 passes an operate function (rva::ObjectOperateTable)
struct ObjectOperate {
	d2r::Game* game;
	d2r::Unit* object;
	d2r::Unit* player;
	uint64_t seed;
	int32_t classId;
};
