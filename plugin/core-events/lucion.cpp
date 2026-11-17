// Lucion's death (ProjectDiablo.dll 0x10376560; docs/pd2/uber-bosses-dll.md, sections 5 and 6). When Lucion
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

#include "pd2_engine.h"
#include "log.h"
#include "arena_spots.h"
#include "map_open.h"
#include "corruption_roll.h"
#include "altar_panel.h"
#include "placements.h"

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
// PD2's levels 188 and 189; the levels are bound in placements/lucion.cpp
const auto Arena() noexcept { return placements::LevelOf("lucion"); }
const auto Temple() noexcept { return placements::LevelOf("temple"); }
constexpr int32_t DarkPortal = 622;           // xactdarkportal_grey (objects.txt row, pinned by test_object_ids)
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
std::atomic<uint32_t> g_attempts {0};
std::atomic<uint32_t> g_desecrated {0};

constexpr int32_t AttemptedStage = 9;  // PD2's roll sets 9: one attempt per kill
constexpr int32_t TypeAmulet = 12;     // itemtypes amul
// merged itemstatcost corrupted, desecrated, mirrored (PD2 360, 206, 486)
constexpr int32_t StatCorrupted = 413, StatDesecrated = 382, StatMirrored = 539;

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
	placements::Spot at{0, 0};  // Cartographer's `temple-portal` spot (arenaspots.txt, #962)
	const bool named = ArenaSpotOf("lucion", "temple-portal", &at);
	int32_t xy[2] = {at.x, at.y};
	void* room = named ? d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAtXY)(near, xy[0], xy[1]) : nullptr;
	if (room == nullptr) {
		d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(UnitCoords)(lucion, xy);
		room = near;
		plog::Warn("lucion: (%d, %d) is %s, the Temple portal goes by the body at (%d, %d)", at.x, at.y,
		           named ? "out of reach" : "not a named spot", xy[0], xy[1]);
	}
	Unit* portal = nullptr;
	const int32_t made = d2r::Engine<int32_t (*)(Game*, Unit*, void*, int32_t, int32_t, int32_t, Unit**, int32_t, int32_t,
	                                             int32_t) noexcept>(PortalMaker)(game, nullptr, room, xy[0], xy[1], Temple(),
	                                                                             &portal, DarkPortal, 0, 1);
	if (made == 0 || portal == nullptr) {
		return false;
	}
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetPortalLevel)(portal, Temple());
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
	if (room == nullptr || OpenPortalAt(game, room, DarkPortal, x, y, Arena()) == nullptr) {
		return false;
	}
	plog::Info("lucion: Harrogath portal at (%d, %d)", x, y);
	return true;
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
			    d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) == 0 && LevelOf(unit) == Arena()) {
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
	    event->levelId != Arena()) {
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

// PD2 0x10356e00, as D2R's pet kill 0x500fb0 sets the death mode: no drop, then mode 0
auto KillQuietly(Game* game, Unit* monster) noexcept -> void {
	using namespace d2r::rva;
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(SetUnitFlags)(monster, UnitNoDrop, 1);
	alignas(8) uint8_t block[0x30] = {};
	d2r::Engine<void (*)(Unit*, int32_t, void*) noexcept>(ModeCmdInit)(monster, ModeDeath, block);
	*reinterpret_cast<void**>(block + 0x10) = d2r::Engine<void* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, monster);
	d2r::Engine<uint64_t (*)(Game*, void*, int32_t) noexcept>(ModeCmdRun)(game, block, 1);
}

namespace {

namespace items = D2RL::Items;

// UB7b, the brick (PD2 0x103729e0's failure branch): a new rare `amu` at item level 99 replaces the amulet, then
// 0x103649d0 rolls a normal Corruption (flag 0) and the Desecration roll (flag 1) on it. Nothing is read from the old
// amulet. The swap is one SDK item transaction (old amulet in, new one out into the inventory), run on the game thread
// by the altar panel's confirm (altar_panel.cpp). The two rolls are native (editNativeItem): the SDK cannot pick a
// cubemain corruption row.
// The brick is unidentified, as PD2's: 0x1036ebc0 leaves the drop request's force flag and item-flag override at 0.
const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
const uint32_t AmuletCode = items::MakeItemCode("amu");
constexpr uint32_t BrickLevel = 99;
std::atomic<uint32_t> g_bricks {0};
Game* g_brickGame = nullptr;  // the confirm's game and player, for the rolls and SendItem
Unit* g_brickPlayer = nullptr;

struct Rolls {
	bool corrupted = false;
	bool desecrated = false;
};

// PD2's two 0x103649d0 calls on the new amulet, then a refresh on the client (item flag 1), as UB7a's success
void RollBrick(const D2RL::PluginContext*, void* native, void* user) noexcept {
	using namespace d2r::rva;
	auto* item = static_cast<Unit*>(native);
	auto& rolls = *static_cast<Rolls*>(user);
	rolls.corrupted = CorruptFreshItem(g_brickGame, item);
	rolls.desecrated = DesecrateItem(g_brickGame, item);
	d2r::Engine<void (*)(Game*, Unit*, Unit*, uint32_t, uint32_t, int32_t, uint8_t, uint8_t, uint32_t) noexcept>(SendItem)(
		g_brickGame, g_brickPlayer, item, 1, 0, At<int32_t>(item, UnitMode), d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemPage)(item),
		d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemStorePage)(item), d2r::Engine<uint32_t (*)(Unit*) noexcept>(UnitPackedXY)(item));
}

}  // namespace

// Game thread: the swap, then the rolls. `old` is the amulet in the inventory; false when the transaction failed.
auto BrickAmulet(Game* game, Unit* playerUnit, D2RL::PlayerHandle player, D2RL::ItemHandle old) noexcept -> bool {
	if (g_items == nullptr) {
		plog::Info("desecration: no brick, the SDK item services are off");
		return false;
	}
	g_brickGame = game;
	g_brickPlayer = playerUnit;
	const items::TransactionInput input {
		.item = old, .quantity = 1, .socketedItemPolicy = items::SocketedItemPolicy::RejectIfNotEmpty};
	const items::ItemCreateSpec spec {.structSize = items::ItemCreateSpecSize,
	                                  .code = AmuletCode,
	                                  .quality = items::Quality::Rare,
	                                  .qualityRecordId = items::RandomQualityRecord,
	                                  .itemLevel = BrickLevel,
	                                  .seedMode = items::SeedMode::Random,
	                                  .quantity = items::DefaultValue,
	                                  .durability = items::DefaultValue,
	                                  .stateFlags = 0,  // unidentified (top of block)
	                                  .destination = {.structSize = items::ItemDestinationSize,
	                                                  .container = items::ItemContainer::Inventory,
	                                                  .placement = items::Placement::Automatic}};
	D2RL::ItemHandle brick = D2RL::InvalidItemHandle;
	const items::Transaction transaction {.structSize = items::TransactionSize,
	                                      .player = player,
	                                      .inputCount = 1,
	                                      .outputCount = 1,
	                                      .inputs = &input,
	                                      .outputs = &spec,
	                                      .outputItems = &brick,
	                                      .outputCapacity = 1};
	items::TransactionResult result {.structSize = items::TransactionResultSize};
	const items::Result status = g_items->executeTransaction(g_context, &transaction, &result);
	if (status != items::Result::Success || brick == D2RL::InvalidItemHandle) {
		plog::Info("desecration: no brick, the transaction failed (%u); the amulet stays", static_cast<uint32_t>(status));
		return false;
	}
	Rolls rolls;
	const items::Result edited = g_items->editNativeItem(g_context, brick, RollBrick, &rolls);
	plog::Info("desecration: brick %u, a rare amulet at item level %u replaces the amulet: corruption %s, "
	           "desecration %s (edit %u)",
	           ++g_bricks, BrickLevel, rolls.corrupted ? "rolled" : "NOT rolled", rolls.desecrated ? "rolled" : "NOT rolled",
	           static_cast<uint32_t>(edited));
	return true;
}

namespace {

template <class Service>
auto Query(const D2RL::PluginContext* context, const Service*& service, bool (*has)(const Service*, uint32_t),
           uint32_t required) noexcept -> bool {
	if (context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !has(service, required)) {
		service = nullptr;
	}
	return service != nullptr;
}

}  // namespace

// UB7a, Desecration (PD2's altar handler 0x103961b0 and roll 0x103729e0; docs/pd2/uber-bosses-dll.md, section 8)
// at the Sacrifice Altar's panel (altar_panel.cpp). `item` is a corrupted amulet that is not desecrated or mirrored,
// and Lucion is dead (stage 8): the attempt sets the stage to 9, then succeeds at 50% (60% at tiers 1-2) with one of
// rows 682-689 as desecrate/desecratenum. Native: the SDK cannot add a mod to an existing item. Failed means the roll
// failed and the caller makes the brick (BrickAmulet); Refused changed nothing and says why in the log.
auto Desecrate(Game* game, Unit* player, Unit* item) noexcept -> DesecrateResult {
	using namespace d2r::rva;
	if (game == nullptr || player == nullptr || item == nullptr) {
		return DesecrateResult::Refused;
	}
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const char* why = d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType)(item, TypeAmulet) == 0 ? "not an amulet"
	                : stat(item, StatCorrupted, 0) == 0                                                    ? "not corrupted"
	                : stat(item, StatDesecrated, 0) != 0                                                   ? "already desecrated"
	                : stat(item, StatMirrored, 0) != 0                                                     ? "mirrored"
	                                                                                                       : nullptr;
	if (why != nullptr) {
		plog::Info("desecration: refused (%s)", why);
		return DesecrateResult::Refused;
	}
	int32_t* stage = CounterOf(game, LucionStageKey);
	if (*stage != LucionDeadStage) {
		plog::Info("desecration: refused (the stage is %d, not %d)", *stage, LucionDeadStage);
		return DesecrateResult::Refused;
	}
	*stage = AttemptedStage;
	const int32_t tier = *CounterOf(game, TierKey);
	const int32_t chance = tier == 1 || tier == 2 ? 60 : 50;
	const auto roll = static_cast<int32_t>(d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(player), 100));
	const bool success = roll < chance && DesecrateItem(game, item);
	if (success) {
		// Refresh the amulet on the client, as core-rules' reroll refreshes an item (item flag 1)
		d2r::Engine<void (*)(Game*, Unit*, Unit*, uint32_t, uint32_t, int32_t, uint8_t, uint8_t, uint32_t) noexcept>(SendItem)(
			game, player, item, 1, 0, At<int32_t>(item, UnitMode), d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemPage)(item),
			d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemStorePage)(item), d2r::Engine<uint32_t (*)(Unit*) noexcept>(UnitPackedXY)(item));
		g_desecrated.fetch_add(1, std::memory_order_relaxed);
	}
	plog::Info("desecration: attempt %u at tier %d, roll %d of 100 against %d: %s, stage %d", ++g_attempts, tier, roll,
	           chance, success ? "desecrated" : "failed", AttemptedStage);
	return success ? DesecrateResult::Desecrated : DesecrateResult::Failed;
}

// The Sacrifice Altar (class 621, operate slot 35, map_events.cpp): PD2 takes the amulet on the cursor through its
// panel; here the altar opens the plugin panel with one amulet slot (altar_panel.cpp) and confirm runs Desecrate.
// shortcut: the altar stays in mode 0 (PD2 sets mode 1 after an attempt), the HD altar may have no mode-1 art; upgrade once checked
auto OperateSacrificeAltar(Game* game, Unit* player) noexcept -> int32_t {
	if (game != nullptr && player != nullptr) {
		OpenAltarPanel(game, player);
	}
	return 1;
}

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
	g_context = context;
	const bool brick = Query(context, g_items, D2RL::HasItemServiceField, D2RL::ItemServiceRequiredSize);
	std::snprintf(report, reportSize,
	              "Lucion's death: Temple and Harrogath portals, Demonic Cube roll, stage %d, adds killed; "
	              "Desecration's brick %s",
	              LucionDeadStage, brick ? "on" : "OFF (no SDK item service)");
	return 1;
}

auto LucionCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size,
	              "lucion: %u deaths, %u Temple portals, %u Harrogath portals, %u Demonic Cubes, %u adds killed, "
	              "%u desecration attempts, %u desecrated, %u bricks",
	              g_deaths.load(), g_portals.load(), g_returns.load(), g_cubes.load(), g_killed.load(), g_attempts.load(),
	              g_desecrated.load(), g_bricks.load());
}
