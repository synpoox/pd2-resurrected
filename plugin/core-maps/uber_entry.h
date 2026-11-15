// PD2's uber entry (uber_entry.cpp): the Blood Fountain opening, the portal entry's area rules, the grey portals and
// the uber levels' rooms and waypoints. map_open.cpp's item spell and portal hooks call these.
#pragma once

#include "open_gate.h"
#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// The port's uber levels: Diablo Clone, Rathma, Lucion and Uber Tristram.
auto IsUberLevel(int32_t level) noexcept -> bool;
// True, with the reason in `why`, when the player is too far from the Blood Fountain to open an uber area.
auto UberFountainRefusal(d2r::Unit* player, char* why, size_t size) noexcept -> bool;
// Opens uber `level` with the key `item`: grey re-entry portals at tier 0, then every player at the fountain is warped in.
auto OpenUber(d2r::Game* game, d2r::Unit* player, d2r::Unit* item, int32_t level) noexcept -> void;
// The portal operate's two uber halves (map_open.cpp's OperatePortal runs the Outer Void link between them):
// true when `Begin` already finished the operate with `*result`; `Finish` warps the player and returns the operate result.
auto UberPortalBegin(ObjectOperate* args, int32_t level, int32_t* result) noexcept -> bool;
auto UberPortalFinish(ObjectOperate* args, int32_t level) noexcept -> int32_t;
// Hooks D2Game's room-release check so a room holding a live uber boss is kept; false when the bytes differ.
auto InstallBossRoomKeep(const D2RL::PluginContext* context) noexcept -> bool;
// True for a waypoint preset in an uber level, which PD2 leaves without one: the preset spawn skips it.
auto NoUberWaypoint(d2r::Game* game, void* room, const uint8_t* preset) noexcept -> bool;

// The uber keys' item spell (pSpell 19, #951): writes the stub slot 0 of D2R's item spell check and use handler tables.
// Returns how many of the two writes took.
auto InstallUberKey(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto UberKeyCounters(char* out, size_t size) noexcept -> void;

// The uber entry's counters, for map_open's status line.
struct UberEntryStats {
	uint32_t opened, warped, portals, entries, waypoints, bossRoomsKept;
};
auto UberEntryCounts() noexcept -> UberEntryStats;
