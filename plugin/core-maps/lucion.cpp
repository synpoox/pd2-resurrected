// Lucion's death (ProjectDiablo.dll 0x10376560; docs/research/pd2-uber-bosses-dll.md, sections 5 and 6). When Lucion
// dies PD2 opens a dark grey portal (object 622, the port's 625) at (0x6221, 0x17df) in his arena to the Temple of the
// Triune, with its return portal in the Temple; at tiers 1-2 it drops a Demonic Cube (`imrn`) at 1 in (4 - tier) x 25;
// it sets the fight's stage to 8, which the Temple's altar needs (UB7); and it kills the adds left in the arena
// without drops (Shivering Child, Smouldering Defiler, Anguished Mother and the spawner) and removes his controller.
// It also opens a dark grey portal in Harrogath back into the arena (PD2 0x1038a1f0 at Harrogath's spot 1).
//
// The trigger is the SDK's monster-death listener, as in key_drops.cpp. No SDK service makes a portal, finds a level
// spot, kills a monster or drops an item at a dead monster, so those are engine calls.
// shortcut: the adds are found by class in the arena, where PD2 does it only once the Overseer AI has registered the
// controller (game +0x2604); use that once the phase scripts are ported
// shortcut: PD2 links the Harrogath portal to an `empty` object (587, the port's 590) at the arena's entry spot less
// (5, 5) and removes the waypoints by it (0x1038a510); here the portal's operate (map_open.cpp) warps to the arena's
// entry spot itself and the arena keeps any waypoint. Port the link if PD2's arrival spot ever differs in game.
#include "lucion.h"

#include "d2r.h"
#include "log.h"
#include "map_open.h"

#include <D2RLPlugin/item.h>
#include <D2RLPlugin/lifecycle_events.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t Lucion = 1129;      // PD2 1112, in the port's monstats ids (PD2's + 17)
constexpr int32_t Controller = 1130;  // LucionControl (PD2 1113): PD2 removes the unit at game +0x2604
// PD2's kill list 0x45a, 0x45b, 0x474, 0x475: LucionSpawn, LucionSpawnRanged, LucionSpawnTank, LucionSpawner
constexpr int32_t Adds[] = {1131, 1132, 1157, 1158};
constexpr int32_t Arena = 184, Temple = 185;  // PD2's levels 188 and 189
constexpr int32_t DarkPortal = 625;           // xactdarkportal_grey (PD2 622)
constexpr int32_t TempleX = 0x6221, TempleY = 0x17df;
constexpr int32_t Harrogath = 109, HarrogathSpot = 1, HarrogathAct = 4;  // PD2's 0x6d, spot 1; act V
constexpr int32_t PortalSize = 3;  // the unit size PD2 0x1038a000 passes the spot search
constexpr uint32_t TierKey = 0x55425254;  // 'UBRT', as map_open.cpp
const uint32_t CubeCode = D2RL::Items::MakeItemCode("imrn");
constexpr int MaxAdds = 128;

// PD2's Demonic Cube odds: 1 in (4 - tier) x 25 at tiers 1-2, none at tier 0
constexpr auto CubeOdds(int32_t tier) noexcept -> uint32_t {
	return tier >= 1 && tier <= 2 ? static_cast<uint32_t>((4 - tier) * 25) : 0;
}
static_assert(CubeOdds(0) == 0 && CubeOdds(1) == 75 && CubeOdds(2) == 50 && CubeOdds(3) == 0);

std::atomic<uint32_t> g_deaths {0};
std::atomic<uint32_t> g_portals {0};
std::atomic<uint32_t> g_returns {0};
std::atomic<uint32_t> g_cubes {0};
std::atomic<uint32_t> g_killed {0};

auto CounterOf(Game* game, uint32_t key) noexcept -> int32_t* {
	return d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

auto LevelOf(Unit* unit) noexcept -> int32_t {
	using namespace d2r::rva;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit);
	return room != nullptr ? d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room) : -1;
}

// PD2 0x1038a300: the portal at its spot, else by Lucion when that spot is out of his rooms' reach
auto OpenTemplePortal(Game* game, Unit* lucion) noexcept -> bool {
	using namespace d2r::rva;
	void* near = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(lucion);
	if (near == nullptr) {
		return false;
	}
	int32_t xy[2] = {TempleX, TempleY};
	void* room = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAt)(near, xy[0], xy[1]);
	if (room == nullptr) {
		d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(lucion, xy);
		room = near;
		plog::Warn("lucion: (0x%x, 0x%x) is out of reach, the Temple portal goes by the body at (%d, %d)", TempleX,
		           TempleY, xy[0], xy[1]);
	}
	Unit* portal = nullptr;
	const int32_t made = d2r::Engine<int32_t (*)(Game*, Unit*, void*, int32_t, int32_t, int32_t, Unit**, int32_t, int32_t,
	                                             int32_t) noexcept>(PortalMaker)(game, nullptr, room, xy[0], xy[1], Temple,
	                                                                             &portal, DarkPortal, 0, 1);
	if (made == 0 || portal == nullptr) {
		return false;
	}
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetPortalLevel)(portal, Temple);
	return true;
}

// PD2 0x1038a1f0(game, 0x6d, spot 1, object 622): a dark grey portal at Harrogath's spot 1 into the arena
auto OpenHarrogathPortal(Game* game) noexcept -> bool {
	using namespace d2r::rva;
	if (At<uint64_t>(game, GameActCount) <= HarrogathAct) {
		return false;
	}
	void* act = At<void**>(game, GameActs)[HarrogathAct];
	int32_t x = 0, y = 0;
	void* room = act != nullptr
		? d2r::Engine<void* (*)(uint32_t, void*, int32_t, int32_t, int32_t*, int32_t*, int32_t) noexcept>(ActSpawnSpot)(
			  d2r::DataFlag(game), act, Harrogath, HarrogathSpot, &x, &y, PortalSize)
		: nullptr;
	if (room == nullptr || OpenPortalAt(game, room, DarkPortal, x, y, Arena) == nullptr) {
		return false;
	}
	plog::Info("lucion: Harrogath portal at (%d, %d)", x, y);
	return true;
}

// PD2 0x10356e00, as D2R's pet kill 0x500fb0 sets the death mode: no drop, then mode 0
auto KillQuietly(Game* game, Unit* monster) noexcept -> void {
	using namespace d2r::rva;
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(SetUnitFlags)(monster, UnitNoDrop, 1);
	alignas(8) uint8_t block[0x30] = {};
	d2r::Engine<void (*)(Unit*, int32_t, void*) noexcept>(MonsterModeBlock)(monster, ModeDeath, block);
	*reinterpret_cast<void**>(block + 0x10) = d2r::Engine<void* (*)(Game*, Unit*) noexcept>(UnitTarget)(game, monster);
	d2r::Engine<uint64_t (*)(Game*, void*, int32_t) noexcept>(SetMonsterMode)(game, block, 1);
}

// The adds and the controller alive in the arena, picked first so no kill runs while the lists are walked
auto KillAdds(Game* game) noexcept -> int32_t {
	using namespace d2r::rva;
	Unit* found[MaxAdds] = {};
	int n = 0;
	for (int bucket = 0; bucket < 128; ++bucket) {
		for (auto* unit = At<Unit*>(game, GameMonsters + bucket * 8); unit != nullptr && n < MaxAdds;
		     unit = At<Unit*>(unit, UnitListNext)) {
			const int32_t classId = At<int32_t>(unit, UnitClassId);
			if ((classId == Controller || std::find(std::begin(Adds), std::end(Adds), classId) != std::end(Adds)) &&
			    d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) == 0 && LevelOf(unit) == Arena) {
				found[n++] = unit;
			}
		}
	}
	for (int i = 0; i < n; ++i) {
		KillQuietly(game, found[i]);
	}
	return n;
}

void OnMonsterDeath(const D2RL::PluginContext* context, const D2RL::Lifecycle::MonsterDeathEvent* event,
                    void* userData) noexcept {
	using namespace d2r::rva;
	(void)context;
	(void)userData;
	if (event == nullptr || event->monster.type != D2RL::Lifecycle::UnitType::Monster || event->gameId > 0xffff ||
	    event->levelId != Arena) {
		return;
	}
	const auto id = static_cast<uint16_t>(event->gameId);
	const uint64_t key = d2r::Engine<uint64_t (*)(uint16_t) noexcept>(GameKey)(id);
	Game* game = key != 0 ? d2r::Engine<Game* (*)(uint64_t) noexcept>(GameById)(key) : nullptr;
	if (game == nullptr || At<uint16_t>(game, GameIdField) != id) {
		return;
	}
	Unit* lucion = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(FindUnit)(game, 1, event->monster.id);
	if (lucion == nullptr || At<int32_t>(lucion, UnitClassId) != Lucion) {
		return;
	}
	const uint32_t deaths = g_deaths.fetch_add(1, std::memory_order_relaxed) + 1;
	const bool portal = OpenTemplePortal(game, lucion);
	if (portal) {
		g_portals.fetch_add(1, std::memory_order_relaxed);
	}
	const bool back = OpenHarrogathPortal(game);
	if (back) {
		g_returns.fetch_add(1, std::memory_order_relaxed);
	}
	const int32_t tier = *CounterOf(game, TierKey);
	const uint32_t odds = CubeOdds(tier);
	auto* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(lucion);
	bool cube = false;
	if (odds > 0 && d2r::RandomBelow(seed, odds) == 0) {
		const int32_t row =
			d2r::Engine<int32_t (*)(uint8_t, uint32_t) noexcept>(ItemRowByCode)(static_cast<uint8_t>(d2r::DataFlag(game)), CubeCode);
		cube = row >= 0 && d2r::Engine<void* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t) noexcept>(DropItemAt)(
		                       game, lucion, row, QualityNormal, 0, 0) != nullptr;
		if (cube) {
			g_cubes.fetch_add(1, std::memory_order_relaxed);
		}
	}
	*CounterOf(game, LucionStageKey) = LucionDeadStage;
	const int32_t killed = KillAdds(game);
	g_killed.fetch_add(static_cast<uint32_t>(killed), std::memory_order_relaxed);
	plog::Info("lucion: dead at tier %d (%u so far): Temple portal %s, Harrogath portal %s, Demonic Cube 1 in %u %s, "
	           "%d adds killed, stage %d",
	           tier, deaths, portal ? "open" : "FAILED", back ? "open" : "FAILED", odds, cube ? "dropped" : "not dropped",
	           killed, LucionDeadStage);
}

}  // namespace

auto InstallLucion(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
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
	std::snprintf(report, reportSize,
	              "Lucion's death: Temple and Harrogath portals, Demonic Cube roll, stage %d, adds killed",
	              LucionDeadStage);
	return 1;
}

auto LucionCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "lucion: %u deaths, %u Temple portals, %u Harrogath portals, %u Demonic Cubes, %u adds killed",
	              g_deaths.load(), g_portals.load(), g_returns.load(), g_cubes.load(), g_killed.load());
}
