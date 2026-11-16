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
#include "corruption_roll.h"
#include "log.h"
#include "open_gate.h"
#include "placements.h"
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

// lesserhydra1/2 (PD2 0x2e2, 0x2e3): the placeholder presets PD2's preset rewrite (0x1038bfc0, 0x1038b990) turned into the
// uber fights' monsters
constexpr int32_t PlaceholderA = 755, PlaceholderB = 756;
constexpr int32_t LucionClass = 1129, LucionControlClass = 1130;
struct Prime {
	int32_t classId;
	int32_t bit;
};
constexpr Prime Primes[] = {{704, 2}, {705, 4}, {709, 8}};  // ubermephisto, uberdiablo, uberbaal
// PD2 keeps the bits in game +0x2628, which its Tristram opening (0x10383060) sets to 0 after the warp. The port's +0x2628
// ('UBRE') holds the other ubers' entries, so the bits get their own counter: a preset spawned during the opening's warp
// keeps its bit
constexpr uint32_t PrimesKey = 0x55425442;  // 'UBTB'

// Uber Tristram (0xb9), with the game's area 0x29: lesserhydra1 rolls 1 in 20 for each prime not yet placed, in order, and
// lesserhydra2 takes the first one not placed. Returns the prime, 0 when the preset is no placeholder, -1 to drop it.
// shortcut: PD2 also needs the game's area to be Uber Tristram's (game +0x1df4 == 0x29), which the port does not keep.
auto TristramPrime(d2r::Game* game, int32_t id) noexcept -> int32_t {
	if (id != PlaceholderA && id != PlaceholderB) {
		return 0;
	}
	uint32_t key = PrimesKey;
	int32_t* placed = d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
	if (placed == nullptr) {
		return -1;
	}
	for (const Prime& p : Primes) {
		if ((*placed & p.bit) == 0 && (id == PlaceholderB || RollRand() % 20 < 1)) {
			*placed |= p.bit;
			return p.classId;
		}
	}
	return -1;
}

// The uber levels' preset rewrites (map_bosses.cpp before #964) and waypoint presets. No PluginSDK service spawns or
// reports room presets, so this hooks D2R's preset spawn (rva::PresetSpawn).
auto OnPresetSpawn(d2r::Game* game, void* room, const uint8_t* preset) noexcept -> void {
	using namespace d2r::rva;
	if (game == nullptr || room == nullptr || preset == nullptr) {
		g_presetSpawnOriginal(game, room, preset);
		return;
	}
	if (NoUberWaypoint(game, room, preset)) {
		return;
	}
	if (d2r::At<int32_t>(preset, PresetType) == UnitMonster) {
		const int32_t level = d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room);
		const int32_t id = d2r::At<int32_t>(preset, PresetId);
		auto* mutablePreset = const_cast<uint8_t*>(preset);
		if (level == placements::LevelOf("lucion") && id == PlaceholderA) {
			// 0x1038bfc0 (lesserhydra1): Lucion, then his controller, both on the preset's spot
			for (const int32_t classId : {LucionClass, LucionControlClass}) {
				*reinterpret_cast<int32_t*>(mutablePreset + PresetId) = classId;
				g_presetSpawnOriginal(game, room, preset);
			}
			plog::Info("uber: level %d placeholder %d became Lucion and his controller", level, id);
			return;
		}
		if (level == placements::LevelOf("tristram")) {
			const int32_t prime = TristramPrime(game, id);
			if (prime < 0) {
				return;
			}
			if (prime > 0) {
				*reinterpret_cast<int32_t*>(mutablePreset + PresetId) = prime;
				plog::Info("uber: Tristram placeholder %d became prime %d", id, prime);
			}
		}
	}
	g_presetSpawnOriginal(game, room, preset);
}

// PD2's monster spawn hook 0x1038af90 ran the uber tier scaling for every monster spawning in a PD2 level; the map stats it
// also ran are cut (#964). No SDK service reports or changes a spawning monster, so this hooks D2R. Not MonsterSpawned
// (0x494a60): Cartographer hooks it first and the loader cannot hook one address twice (#1032). Its only caller, the
// spawn core 0x493cc0 (params), is free and returns the monster it made (null when it made none, 1 on the 0x49427c path), so the scaling runs
// as the core returns: after the whole spawn, where PD2's hook ran it right after the class setup.
constexpr uint64_t SpawnCore = 0x493cc0;  // mov [rsp+0x10],rbx / mov [rsp+0x18],rsi
constexpr uint8_t SpawnCoreEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
using SpawnCoreFn = d2r::Unit* (*)(void* params) noexcept;
SpawnCoreFn g_coreOriginal = nullptr;

auto OnSpawnCore(void* params) noexcept -> d2r::Unit* {
	d2r::Unit* monster = g_coreOriginal(params);
	d2r::Game* game = params != nullptr ? *static_cast<d2r::Game**>(params) : nullptr;  // params +0: the game
	// The core returns 1, not a monster, when bit 0 of params+0x30 is set (0x49427c; spawns nothing, #1050): null and 1 are no unit.
	if (game != nullptr && reinterpret_cast<uintptr_t>(monster) > 1 && d2r::At<int32_t>(monster, d2r::rva::UnitType) == d2r::rva::UnitMonster &&
	    IsUberLevel(ai_common::LevelOf(monster))) {
		ScaleUberMonster(game, monster);
	}
	return monster;
}

}  // namespace

auto InstallMapOpen(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	static constexpr uint8_t Empty[8] = {};
	using OperateFn = int32_t (*)(ObjectOperate*) noexcept;
	const bool portal = context->PatchWriteU64(ObjectOperateTable + static_cast<uint64_t>(ObjectOperateFree60) * 8,
	                                           Empty, sizeof(Empty),
	                                           reinterpret_cast<uint64_t>(static_cast<OperateFn>(OperatePortal)));
	const bool rooms = InstallBossRoomKeep(context);
	const bool presets = context->InstallInlineHook(PresetSpawn, PresetSpawnEntry, sizeof(PresetSpawnEntry),
	                                                OnPresetSpawn, &g_presetSpawnOriginal);
	const bool spawns = context->InstallInlineHook(SpawnCore, SpawnCoreEntry, sizeof(SpawnCoreEntry), OnSpawnCore,
	                                               &g_coreOriginal);
	std::snprintf(report, reportSize, "portal operate %d%s, boss rooms kept%s, uber waypoints%s, uber scaling%s", ObjectOperateFree60,
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
