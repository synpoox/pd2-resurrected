// PD2's map events that open areas (MS9; docs/research/pd2-map-system-dll.md, section 4). So far Horazon
// (ProjectDiablo.dll 0x10377ca0): when `summonerMap` dies in a tiered map, a red portal opens where it fell, to a random
// tiered map of its level's tier or the next (never its own level), with the open map's area stats; and it drops maps:
// from "Map Tier 1", "Map Tier 2" and "Map Tier 3" 3/2/1 picks in a tier-1 map, 1/3/2 in tier 2, 1/2/3 in tier 3; and
// once the portal is open, 10% for a unique map (key_drops.cpp's DropUniqueMap).
//
// PD2 0x103760a0 tests the dying monster's level against its three tier sets (0x105deb18, 0x105debf4, 0x105deb08), and
// the handler pools the matching set and the next. The trigger is the SDK's monster-death listener; the portal pair
// and the TC drop are engine calls, as no SDK service makes either. The listener also hands the Altar of the
// Catalyst's guardian to map_events.cpp, and the Void/Tomb event takes its portal pair from here.
#include "event_portals.h"

#include "key_drops.h"
#include "log.h"
#include "map_events.h"
#include "map_stats.h"

#include <D2RLPlugin/lifecycle_events.h>

#include <atomic>
#include <cstdio>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t SummonerMap = 936;  // monstats `summonerMap` (PD2 919), port id
constexpr int32_t HorazonEvent = 12;
constexpr int32_t StatLevel = 12;
// PD2's three level lists (section 4) in the port's level ids (merge/ids-levels.tsv)
constexpr int32_t Tiers[3][10] = {
	{144, 146, 147, 149, 152, 156, 158, 165, 166, 199},
	{142, 143, 151, 157, 159, 170, 171, 189, 200, 201},
	{140, 145, 148, 150, 155, 167, 186, 187, 197, 0},
};
constexpr const char* TierTcs[3] = {"Map Tier 1", "Map Tier 2", "Map Tier 3"};
constexpr int32_t Picks[3][3] = {{3, 2, 1}, {1, 3, 2}, {1, 2, 3}};  // by the level's tier, then by TC
// PD2's game +0x2628 must be 1 (the map's portal) and goes up with Horazon's portal: after it, a death does nothing
constexpr uint32_t HorazonKey = 0x48525a4e;  // 'HRZN'

std::atomic<uint32_t> g_deaths {0};
std::atomic<uint32_t> g_portals {0};
std::atomic<uint32_t> g_failed {0};
std::atomic<uint32_t> g_drops {0};

auto TierOf(int32_t level) noexcept -> int {
	for (int t = 0; t < 3; ++t) {
		for (const int32_t l : Tiers[t]) {
			if (l == level && l != 0) {
				return t;
			}
		}
	}
	return -1;
}

// The tier's levels and the next tier's, without `level`; then one of them by the monster's seed. 0 when none.
auto DrawLevel(int tier, int32_t level, uint32_t* seed) noexcept -> int32_t {
	int32_t pool[20] = {};
	uint32_t n = 0;
	for (int t = tier; t < 3 && t <= tier + 1; ++t) {
		for (const int32_t l : Tiers[t]) {
			if (l != 0 && l != level) {
				pool[n++] = l;
			}
		}
	}
	return n > 0 ? pool[d2r::RandomBelow(seed, n)] : 0;
}

// OpenRedPortal without its town check: a free spot by the unit, then the portal pair to the level.
auto OpenPortal(Game* game, Unit* by, int32_t level) noexcept -> bool {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(by);
	if (room == nullptr) {
		return false;
	}
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(by, xy);
	void* found = nullptr;
	d2r::Engine<void (*)(Game*, void*, int32_t*, uint32_t, uint32_t, void**, uint64_t, int32_t) noexcept>(FindFreeSpot)(
		game, room, xy, 3, 0x400, &found, 5, 100);
	return found != nullptr && OpenEventPortal(game, found, xy[0], xy[1], level) != nullptr;
}

// PD2's three drop batches. `player` is the quality roll's player (its magic find).
auto DropMaps(Game* game, Unit* monster, Unit* player, int tier) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const uint8_t* tables = d2r::Engine<const uint8_t* (*)(uint8_t) noexcept>(DataTablesOf)(dataFlag);
	const int32_t ilvl = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(monster, StatLevel, 0);
	int32_t dropped = 0;
	for (int i = 0; tables != nullptr && i < 3; ++i) {
		const int32_t index = d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(TcIndexByName)(
			At<void*>(tables, TablesTcNames), TierTcs[i], 0);
		const uint8_t* tc = index > 0 ? d2r::Engine<const uint8_t* (*)(uint8_t, uint16_t, int32_t) noexcept>(TcRecord)(
											dataFlag, static_cast<uint16_t>(index), 0)
		                              : nullptr;
		if (tc == nullptr) {
			plog::Warn("horazon: no TC \"%s\"", TierTcs[i]);
			continue;
		}
		for (int32_t n = 0; n < Picks[tier][i]; ++n) {
			d2r::Engine<void (*)(Game*, Unit*, Unit*, const uint8_t*, int32_t, int32_t, int32_t, void*, int32_t*,
			                     int32_t) noexcept>(TcDrop)(game, monster, player, tc, 0, ilvl, 0, nullptr, nullptr, 1);
			++dropped;
		}
	}
	return dropped;
}

void OnMonsterDeath(const D2RL::PluginContext* context, const D2RL::Lifecycle::MonsterDeathEvent* event,
                    void* userData) noexcept {
	using namespace d2r::rva;
	using D2RL::Lifecycle::UnitType;
	(void)context;
	(void)userData;
	if (event == nullptr || event->monster.type != UnitType::Monster ||
	    (event->monster.classId != SummonerMap && event->monster.classId != AltarGuardian) || event->gameId > 0xffff) {
		return;
	}
	const int tier = TierOf(event->levelId);
	const auto id = static_cast<uint16_t>(event->gameId);
	const uint64_t key = d2r::Engine<uint64_t (*)(uint16_t) noexcept>(GameKey)(id);
	Game* game = key != 0 ? d2r::Engine<Game* (*)(uint64_t) noexcept>(GameById)(key) : nullptr;
	if (tier < 0 || game == nullptr || At<uint16_t>(game, GameIdField) != id) {
		return;
	}
	Unit* monster = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(FindUnit)(game, 1, event->monster.id);
	if (event->monster.classId == AltarGuardian) {
		AltarGuardianDied(game, monster);
		return;
	}
	// shortcut: a kill by a missile, a pet or a console cheat rolls for the game's first player, which offline is the
	// only one; find the missile's or pet's owner if multiplayer ever needs it
	Unit* killer = event->killer.type == UnitType::Player
		? d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(FindUnit)(game, 0, event->killer.id)
		: nullptr;
	auto* client = At<uint8_t*>(game, GameClients);
	killer = killer != nullptr || client == nullptr ? killer
		: d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(ClientUnit)(client, 0);
	if (monster == nullptr || killer == nullptr) {
		return;
	}
	if (MapEvent(game) != HorazonEvent) {  // PD2 runs it only while the event is Horazon's (game +0x1df4 == 12)
		plog::Info("horazon: died in level %d outside a Horazon event (event %d)", event->levelId, MapEvent(game));
		return;
	}
	const auto opened = [game]() noexcept {  // fetched each time: a counter call may insert and move the others
		uint32_t counter = HorazonKey;
		return d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(GameCounter)(
			reinterpret_cast<uint8_t*>(game) + GameCounters, &counter);
	};
	if (opened() == nullptr || *opened() > 0) {
		return;
	}
	++g_deaths;
	const int32_t level = DrawLevel(tier, event->levelId, d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(monster));
	if (level != 0) {
		ApplyMapArea(game, level);
	}
	if (level != 0 && OpenPortal(game, monster, level)) {
		*opened() += 1;
		plog::Info("horazon: died in level %d (tier %d), red portal to level %d (%u)", event->levelId, tier + 1, level,
		           ++g_portals);
		// PD2 rolls rand() % 10 < 1 for 0x10381510, the unique map drop, once the portal is open
		if (d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(monster), 10) == 0) {
			plog::Info("horazon: 10%% unique map roll hit");
			DropUniqueMap(game, monster);
		}
	} else {
		plog::Warn("horazon: no portal from level %d to %d (%u failed)", event->levelId, level, ++g_failed);
	}
	const int32_t dropped = DropMaps(game, monster, killer, tier);
	g_drops += static_cast<uint32_t>(dropped);
	plog::Info("horazon: %d map picks dropped", dropped);
}

}  // namespace

auto OpenEventPortal(Game* game, void* room, int32_t x, int32_t y, int32_t level) noexcept -> Unit* {
	using namespace d2r::rva;
	Unit* portal = nullptr;
	const bool made = room != nullptr &&
		d2r::Engine<int32_t (*)(Game*, Unit*, void*, int32_t, int32_t, int32_t, Unit**, int32_t, int32_t, int32_t) noexcept>(
			PortalMaker)(game, nullptr, room, x, y, level, &portal, RedPortal, 0, 1) != 0;
	if (made && portal != nullptr) {
		d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetPortalLevel)(portal, level);
	}
	return made ? portal : nullptr;
}

auto InstallEventPortals(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: no lifecycle service");
		return 0;
	}
	const D2RL::Lifecycle::MonsterDeathListener listener {
		.structSize = D2RL::Lifecycle::MonsterDeathListenerSize,
		.flags = 0,
		.callback = OnMonsterDeath,
		.userData = nullptr,
	};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	if (lifecycle->registerMonsterDeathListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
		std::snprintf(report, reportSize, "off: monster-death listener refused");
		return 0;
	}
	std::snprintf(report, reportSize, "Horazon's portal on summonerMap's death");
	return 1;
}

auto EventPortalsCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "horazon deaths %u, portals %u, failed %u, maps dropped %u", g_deaths.load(),
	              g_portals.load(), g_failed.load(), g_drops.load());
}
