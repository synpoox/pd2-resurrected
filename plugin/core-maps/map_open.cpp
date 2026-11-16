// The map lane's portal operate and the boss-room keep (the uber areas' entry is uber_entry.cpp).
//
// PD2's map opening (item spell 12, moved to D2R's spell 16: a right-click on a PD2 map in Harrogath opened a red portal
// to the level its misc `len` names, with the area rules, stat routing and event roll of the open map) is cut with PD2's
// maps (#964): no PD2 map item is left, and Cartographer owns the item spell dispatchers for the native maps (#945).
// The uber keys open through item spell 19 (uber_entry.cpp's use slot patches), not through this file.
//
// What stays here is PD2's portal operate (77 in PD2, D2R's free operate slot 60 here), which the uber areas' grey portals
// use: each runs the area rules on entry, warps the player in and goes (uber_entry.cpp's UberPortalBegin and Finish).
#include "map_open.h"

#include "pd2_engine.h"
#include "ai_common.h"
#include "log.h"
#include "open_gate.h"
#include "uber_entry.h"
#include "uber_scaling.h"

#include <cstdio>

namespace {

// PD2's portal operate (77), in D2R's operate slot 60 (the uber entry's, uber_entry.cpp)
auto OperatePortal(ObjectOperate* args) noexcept -> int32_t {
	const int32_t level = d2r::Engine<uint16_t (*)(d2r::Unit*) noexcept>(d2r::rva::PortalLevel)(args->object);
	int32_t result = 0;
	if (UberPortalBegin(args, level, &result)) {
		return result;
	}
	return UberPortalFinish(args, level);
}

using PresetSpawnFn = void (*)(d2r::Game* game, void* room, const uint8_t* preset) noexcept;
PresetSpawnFn g_presetSpawnOriginal = nullptr;

// The uber levels' waypoint presets are refused at spawn (NoUberWaypoint). No PluginSDK service reports room presets, so
// this hooks D2R's preset spawn (rva::PresetSpawn).
auto OnPresetSpawn(d2r::Game* game, void* room, const uint8_t* preset) noexcept -> void {
	if (game != nullptr && room != nullptr && preset != nullptr && NoUberWaypoint(game, room, preset)) {
		return;
	}
	g_presetSpawnOriginal(game, room, preset);
}

using SpawnedFn = void (*)(void* params, d2r::Unit* monster) noexcept;
SpawnedFn g_spawnedOriginal = nullptr;

// PD2's monster spawn hook 0x1038af90 ran the uber tier scaling for every monster spawning in a PD2 level; the map stats it
// also ran are cut (#964). No SDK service changes a spawning monster's stats, so this hooks D2R's monster spawn.
auto OnMonsterSpawned(void* params, d2r::Unit* monster) noexcept -> void {
	g_spawnedOriginal(params, monster);
	d2r::Game* game = params != nullptr ? *static_cast<d2r::Game**>(params) : nullptr;  // params +0: the game
	if (game != nullptr && monster != nullptr && d2r::At<int32_t>(monster, d2r::rva::UnitType) == d2r::rva::UnitMonster &&
	    IsUberLevel(ai_common::LevelOf(monster))) {
		ScaleUberMonster(game, monster);
	}
}

}  // namespace

auto InstallMapOpen(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	static constexpr uint8_t Empty[8] = {};
	using OperateFn = int32_t (*)(ObjectOperate*) noexcept;
	const bool portal = context->PatchWriteU64(ObjectOperateTable + static_cast<uint64_t>(ObjectOperatePortal) * 8,
	                                           Empty, sizeof(Empty),
	                                           reinterpret_cast<uint64_t>(static_cast<OperateFn>(OperatePortal)));
	const bool rooms = InstallBossRoomKeep(context);
	const bool presets = context->InstallInlineHook(PresetSpawn, PresetSpawnEntry, sizeof(PresetSpawnEntry),
	                                                OnPresetSpawn, &g_presetSpawnOriginal);
	const bool spawns = context->InstallInlineHook(MonsterSpawned, MonsterSpawnedEntry, sizeof(MonsterSpawnedEntry),
	                                               OnMonsterSpawned, &g_spawnedOriginal);
	std::snprintf(report, reportSize, "portal operate %d%s, boss rooms kept%s, uber waypoints%s, uber scaling%s", ObjectOperatePortal,
	              portal ? "" : " FAILED (slot not empty)", rooms ? "" : " FAILED (bytes differ)",
	              presets ? "" : " FAILED (bytes differ)", spawns ? "" : " FAILED (bytes differ)");
	return (portal ? 1 : 0) + (rooms ? 1 : 0) + (presets ? 1 : 0) + (spawns ? 1 : 0);
}

auto MapOpenCount() noexcept -> int {
	return 4;
}

auto MapOpenCounters(char* out, size_t size) noexcept -> void {
	const UberEntryStats u = UberEntryCounts();
	std::snprintf(out, size,
	              "uber areas opened %u, opening attempts failed %u, players warped %u, grey portals %u, portal entries "
	              "%u, waypoints not spawned %u, boss room releases refused %u",
	              u.opened, g_openFailed.load(), u.warped, u.portals, u.entries, u.waypoints, u.bossRoomsKept);
}
