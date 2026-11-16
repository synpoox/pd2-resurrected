// Horazon, split out of event_portals.cpp (#943). PD2's map event Horazon (ProjectDiablo.dll 0x10377ca0): when `summonerMap`
// dies while the game's event is Horazon's, once per game, it drops six native map items rolled from the
// `Cartographer Maps` pool (#959; the PD2 tier pools, map-stat propagation and unique-map roll are gone here).
// The red portal to the second map is Cartographer's cartographer.open-extra.v1 service (#1028, d2r-tools#485).
// The trigger is the SDK's monster-death listener; the TC drop is an engine call, as no SDK service rolls a TC. The
// listener also hands the Altar of the Catalyst's guardian to map_events.cpp.
#include "horazon.h"

#include "event_portals.h"

#include "cartographer_open_extra_v1.h"
#include "log.h"
#include "map_events.h"

#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/plugin_communication.h>

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t SummonerMap = 936;  // monstats `summonerMap` (PD2 919), port id
constexpr int32_t HorazonEvent = 12;
constexpr int32_t StatLevel = 12;
constexpr const char* MapPool = "Cartographer Maps";
constexpr int32_t MapDrops = 6;
// Once per game: Horazon's drops and portal run on the first death only; the counter marks it done
constexpr uint32_t HorazonKey = 0x48525a4e;  // 'HRZN'

std::atomic<uint32_t> g_deaths {0};
std::atomic<uint32_t> g_drops {0};
const D2RL::PluginContext* g_context = nullptr;
D2RL::PluginCommunication::ServiceLease<carto::OpenExtraServiceV1> g_openLease;

// Leased lazily on the death, never at load (Cartographer may load after core-events); a miss is retried next time.
auto OpenExtra() noexcept -> const carto::OpenExtraServiceV1* {
	if (!g_openLease && g_context != nullptr) {
		const D2RL::PluginCommunicationService* communication = nullptr;
		if (g_context->QueryService(&communication) != D2RL::ServiceQueryResult::Success ||
		    !D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize)) {
			return nullptr;
		}
		D2RL::PluginCommunication::Acquire(g_context, communication, carto::OpenExtraServiceProvider,
		                                   carto::OpenExtraServiceName, carto::OpenExtraServiceVersion,
		                                   sizeof(carto::OpenExtraServiceV1), &g_openLease);
	}
	return g_openLease.Get();
}

// Six rolls of the native map pool. `player` is the quality roll's player (its magic find).
auto DropMaps(Game* game, Unit* monster, Unit* player) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const uint8_t* tables = d2r::Engine<const uint8_t* (*)(uint8_t) noexcept>(DataTablesOf)(dataFlag);
	const int32_t ilvl = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(monster, StatLevel, 0);
	const int32_t index = tables == nullptr ? 0 : d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(TcIndexByName)(
		At<void*>(tables, TablesTcNames), MapPool, 0);
	const uint8_t* tc = index > 0 ? d2r::Engine<const uint8_t* (*)(uint8_t, uint16_t, int32_t) noexcept>(TcRecord)(
										dataFlag, static_cast<uint16_t>(index), 0)
	                              : nullptr;
	if (tc == nullptr) {
		plog::Warn("horazon: no TC \"%s\"", MapPool);
		return 0;
	}
	for (int32_t n = 0; n < MapDrops; ++n) {
		d2r::Engine<void (*)(Game*, Unit*, Unit*, const uint8_t*, int32_t, int32_t, int32_t, void*, int32_t*,
		                     int32_t) noexcept>(TcDrop)(game, monster, player, tc, 0, ilvl, 0, nullptr, nullptr, 1);
	}
	return MapDrops;
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
	if (event->monster.classId == AltarGuardian) {
		plog::Info("map events: guardian death heard in level %d, game %u", static_cast<int>(event->levelId), static_cast<unsigned>(event->gameId));
	}
	const auto id = static_cast<uint16_t>(event->gameId);
	const uint64_t key = d2r::Engine<uint64_t (*)(uint16_t) noexcept>(GameKey)(id);
	Game* game = key != 0 ? d2r::Engine<Game* (*)(uint64_t) noexcept>(GameById)(key) : nullptr;
	if (game == nullptr || At<uint16_t>(game, GameIdField) != id) {
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
	*opened() += 1;
	// The portal beside Horazon's body: `killer` is the player the portal opens for, `monster` the unit it opens beside.
	if (const carto::OpenExtraServiceV1* service = OpenExtra(); service == nullptr) {
		plog::Warn("horazon: no open-extra service, no portal");
	} else {
		carto::OpenExtraResultV1 result {};
		result.structSize = sizeof(result);
		if (service->openExtra(game, killer, monster, static_cast<int32_t>(event->levelId), &result)) {
			plog::Info("horazon: portal to level %d (monster level %d)", result.level, result.monsterLevel);
		} else {
			plog::Info("horazon: no portal: %s", result.why);
		}
	}
	const int32_t dropped = DropMaps(game, monster, killer);
	g_drops += static_cast<uint32_t>(dropped);
	plog::Info("horazon: %d map picks dropped", dropped);
}

}  // namespace

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
	g_context = context;
	std::snprintf(report, reportSize, "Horazon's map drops and portal on summonerMap's death");
	return 1;
}

auto EventPortalsCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "horazon deaths %u, maps dropped %u", g_deaths.load(), g_drops.load());
}
