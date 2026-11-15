// PD2's map use (ProjectDiablo.dll 0x383060; docs/research/pd2-map-system-dll.md, section 1): a right-click on a map
// in Harrogath opens a red portal to the level its misc `len` names. The merge moves PD2's map spell 12 to D2R's free
// item spell 16 and writes `len` in the port's level ids, so this hooks D2R's item spell check and use dispatchers and
// handles spell 16 there; every other spell goes on to D2R.
//
// The gates, in PD2's order: Hell, character level 80+, Rite of Passage done in Hell, standing in Harrogath, and no map
// opened yet in this game; then a free spot for the portal (PD2 places it unchecked). A refused map stays in the
// inventory and the player gets D2R's "can't do that" (D2R's caller sends none of its own). The portal is D2R's cube
// "Red Portal" output, which places object 60 by the player and counts the opening per game; its max of 1 on OpenKey
// is the one-per-game gate. PD2's refusal of levels 152, 153 and 255 belongs to the Monastery map, which keeps a blank
// pSpell.
//
// Uber keys (UB2, docs/research/pd2-uber-bosses-dll.md, section 2) use the same spell: their `len` is an uber level.
// One more gate, standing at the Blood Fountain; then no portal: every player at the fountain is warped in, after
// their pets are removed and their buffs stripped (section 3). The key's tier is kept for the fight, and the opening
// counts on OpenKey, the gate shared with maps. A tier-0 key also leaves PD2's three grey portals by the fountain for
// re-entry. They take PD2's portal operate (77 in PD2, D2R's free operate slot 60 here): each runs the area rules on
// entry, warps the player in and goes. An uber level spawns no waypoint (section 3; NoUberWaypoint).
//
// The Pandemonium Talisman `ubtm` (UB3) opens Uber Tristram the same way. PD2's portal entry (0x103a9780) runs the
// area rules only for levels 137, 161-163 and 188, so a grey portal into Uber Tristram runs none;
// the opening's warp (0x1038a680) still strips every player it takes in.
#include "map_open.h"

#include "almanac.h"
#include "pd2_engine.h"
#include "log.h"
#include "map_events.h"
#include "map_stats.h"
#include "open_gate.h"
#include "uber_entry.h"
#include "unique_maps.h"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t MapSpell = 16;
constexpr int32_t Hell = 2;
constexpr int32_t Harrogath = 109;
constexpr int32_t MinCharLevel = 80;
constexpr int32_t StatLevel = 12;      // itemstatcost level
constexpr int32_t RiteOfPassage = 39;  // the Ancients
constexpr int32_t CantDoThat = 0x15;
using CheckFn = uint64_t (*)(Game* game, Unit* player, Unit* item, Unit* target) noexcept;
using UseFn = void (*)(Game* game, Unit* player, Unit* item, Unit* target) noexcept;

CheckFn g_checkOriginal = nullptr;
UseFn g_useOriginal = nullptr;
std::atomic<uint32_t> g_checks {0};
std::atomic<uint32_t> g_refused {0};
std::atomic<uint32_t> g_opened {0};
// Set once the check hook is in: until then the use hook leaves maps to D2R, so no portal opens ungated
std::atomic<bool> g_gated {false};

// The item's misc record: its pSpell picks the map (16) or the Almanac (18), one lookup per check or use
auto ItemRecordOf(Game* game, Unit* item) noexcept -> const uint8_t* {
	using namespace d2r::rva;
	const int32_t row = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitClassOf)(item, __FILE__,
	                                                                                               __LINE__);
	return d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(ItemRecord)(d2r::DataFlag(game), row);
}

auto MapLevel(Game* game, Unit* player, Unit* item, const uint8_t* record) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(uint32_t, Unit*, Unit*, uint32_t) noexcept>(d2r::rva::ItemCalc)(
		d2r::DataFlag(game), player, item, At<uint32_t>(record, d2r::rva::ItemLenCalc));
}

auto Openings(Game* game) noexcept -> int32_t {
	const int32_t* counter = OpenCounter(game, OpenKey);
	return counter != nullptr ? *counter : 0;
}

auto RiteDone(Unit* player) noexcept -> bool {
	using namespace d2r::rva;
	const uint8_t* data = d2r::Engine<const uint8_t* (*)(Unit*) noexcept>(UnitPlayerData)(player);
	void* quests = data != nullptr ? At<void*>(data, PlayerQuests + Hell * 8) : nullptr;
	return quests != nullptr && d2r::Engine<int32_t (*)(void*, int32_t, int32_t) noexcept>(QuestFlag)(
	                                quests, RiteOfPassage, 0) != 0;
}

// OpenRedPortal's own spot search: false when it would find no free spot, which would use the map up for nothing
auto PortalSpotFree(Game* game, Unit* player) noexcept -> bool {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(player);
	if (room == nullptr) {
		return false;
	}
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(player, xy);
	void* found = nullptr;
	d2r::Engine<void (*)(Game*, void*, int32_t*, uint32_t, uint32_t, void**, uint64_t, int32_t) noexcept>(FindFreeSpot)(
		game, room, xy, 3, 0x400, &found, 5, 100);
	return found != nullptr;
}

// True, with the reason in `why`, when PD2 would refuse this map now.
auto Refusal(Game* game, Unit* player, int32_t level, char* why, size_t size) noexcept -> bool {
	const int32_t charLevel = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(
		player, StatLevel, 0);
	const int32_t where = PlayerLevelId(player);
	if (level <= 0 || level > 255) {
		std::snprintf(why, size, "no area (len %d)", level);
	} else if (At<uint8_t>(game, d2r::rva::GameDifficulty) != Hell) {
		std::snprintf(why, size, "not Hell");
	} else if (charLevel < MinCharLevel) {
		std::snprintf(why, size, "character level %d under %d", charLevel, MinCharLevel);
	} else if (!RiteDone(player)) {
		std::snprintf(why, size, "Rite of Passage not done in Hell");
	} else if (where != Harrogath) {
		std::snprintf(why, size, "in level %d, not Harrogath", where);
	} else if (Openings(game) > 0) {
		std::snprintf(why, size, "a map or uber area is already open in this game");
	} else if (IsUberLevel(level)) {
		return UberFountainRefusal(player, why, size);
	} else if (!PortalSpotFree(game, player)) {
		std::snprintf(why, size, "no free spot for the portal here");
	} else {
		return false;
	}
	return true;
}

auto SpellCheck(Game* game, Unit* player, Unit* item, Unit* target) noexcept -> uint64_t {
	const uint8_t* record = game != nullptr && player != nullptr && item != nullptr ? ItemRecordOf(game, item) : nullptr;
	const int32_t spell = record != nullptr ? At<int32_t>(record, d2r::rva::ItemPSpell) : -1;
	if (spell == d2r::rva::UnlimitedIdentifySpell) {
		return 1;
	}
	record = spell == MapSpell ? record : nullptr;
	if (record == nullptr) {
		return g_checkOriginal(game, player, item, target);
	}
	++g_checks;
	const int32_t level = MapLevel(game, player, item, record);
	char why[96];
	if (Refusal(game, player, level, why, sizeof(why))) {
		plog::Info("map: level %d refused: %s (%u refused)", level, why, ++g_refused);
		d2r::Engine<void (*)(Unit*, int32_t, Unit*) noexcept>(d2r::rva::ClientMessage)(player, CantDoThat, player);
		return 0;
	}
	return 1;
}

// PD2's portal operate (77), in D2R's operate slot 60 (the uber entry's, uber_entry.cpp): the map lane's one line in it
// is the Outer Void's portal link, between the uber fight's two halves.
auto OperatePortal(ObjectOperate* args) noexcept -> int32_t {
	const int32_t level = d2r::Engine<uint16_t (*)(Unit*) noexcept>(d2r::rva::PortalLevel)(args->object);
	int32_t result = 0;
	if (UberPortalBegin(args, level, &result)) {
		return result;
	}
	if (OuterVoidWarp(args->game, args->player, args->object)) {
		return 1;
	}
	return UberPortalFinish(args, level);
}

auto SpellUse(Game* game, Unit* player, Unit* item, Unit* target) noexcept -> void {
	const uint8_t* record = game != nullptr && player != nullptr && item != nullptr ? ItemRecordOf(game, item) : nullptr;
	const int32_t spell = record != nullptr ? At<int32_t>(record, d2r::rva::ItemPSpell) : -1;
	if (spell == d2r::rva::UnlimitedIdentifySpell) {
		IdentifyAll(game, player);
		return;
	}
	record = g_gated && spell == MapSpell ? record : nullptr;
	if (record == nullptr) {
		g_useOriginal(game, player, item, target);
		return;
	}
	const int32_t level = MapLevel(game, player, item, record);
	if (IsUberLevel(level)) {
		OpenUber(game, player, item, level);
		return;
	}
	plog::Info("map: opening level %d", level);
	const int32_t opened = d2r::Engine<int32_t (*)(Game*, Unit*, uint32_t, int32_t, int32_t) noexcept>(
		d2r::rva::OpenRedPortal)(game, player, OpenKey, level, 1);
	if (opened != 0) {
		plog::Info("map: red portal to level %d, %u opened", level, ++g_opened);
		RouteMapStats(game, item, level);
		RollMapEvent(game, item, level);
	} else {
		plog::Warn("map: no red portal to level %d (D2R refused), %u failed", level, ++g_openFailed);
	}
}

}  // namespace

auto InstallMapOpen(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	// The use first: with only the check, D2R's use would take a map that passed and open nothing
	const bool use = context->InstallInlineHook(ItemSpellUse, ItemSpellUseEntry, sizeof(ItemSpellUseEntry), SpellUse,
	                                            &g_useOriginal);
	const bool check = use && context->InstallInlineHook(ItemSpellCheck, ItemSpellCheckEntry,
	                                                     sizeof(ItemSpellCheckEntry), SpellCheck, &g_checkOriginal);
	g_gated = check;
	static constexpr uint8_t Empty[8] = {};
	using OperateFn = int32_t (*)(ObjectOperate*) noexcept;
	const bool portal = context->PatchWriteU64(ObjectOperateTable + static_cast<uint64_t>(ObjectOperatePortal) * 8,
	                                           Empty, sizeof(Empty),
	                                           reinterpret_cast<uint64_t>(static_cast<OperateFn>(OperatePortal)));
	const bool rooms = InstallBossRoomKeep(context);
	std::snprintf(report, reportSize, "item spells %d and %d (Almanac)%s, portal operate %d%s, boss rooms kept%s", MapSpell,
	              d2r::rva::UnlimitedIdentifySpell,
	              check ? "" : use ? " FAILED (check bytes differ)" : " FAILED (use bytes differ)", ObjectOperatePortal,
	              portal ? "" : " FAILED (slot not empty)", rooms ? "" : " FAILED (bytes differ)");
	return (check ? 1 : 0) + (portal ? 1 : 0) + (rooms ? 1 : 0);
}

auto MapOpenCount() noexcept -> int {
	return 3;
}

auto MapOpenCounters(char* out, size_t size) noexcept -> void {
	const UberEntryStats u = UberEntryCounts();
	std::snprintf(out, size,
	              "map checks %u, refused %u, opened %u, failed %u; uber areas opened %u, players warped %u, grey "
	              "portals %u, portal entries %u, waypoints not spawned %u, boss room releases refused %u",
	              g_checks.load(), g_refused.load(), g_opened.load(), g_openFailed.load(), u.opened, u.warped, u.portals,
	              u.entries, u.waypoints, u.bossRoomsKept);
}
