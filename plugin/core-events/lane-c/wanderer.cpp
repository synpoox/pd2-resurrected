// The Dark Wanderer (#1006, #1064): PD2's buffed boss that spawns 85 frames after the Wanderer leaves, and his think.
//
// His think: D2R's think (monai row 91, d2r::sig::AiDarkWanderer) idles forever unless quest a3q7's intro byte is set,
// which only the Act 3 docks set, so a map's Wanderer never walks off. This port is that think without the quest check,
// for monstats class 938 (darkwandererMap) only. Class 368 (D2R's own darkwanderer) keeps D2R's think through the
// trampoline. It does not set quest 0x1c, so a3q7's own event and its player callback never run. The leave is D2R's:
// unit flag UnitNoTreasure, then death mode with no kill.
//
// The boss: PD2 (ProjectDiablo.dll's death step and boss functions) plays the death sound, then 85 frames later spawns a boss at
// the Wanderer's corpse: 1 in 3 from pool A (the map bosses), else pool B (the superuniques and Izual, Blood Raven). A boss
// gets its max life multiplied and a stat list with state `mapevent` and a damage and mastery bonus
// (docs/deviations.md, Dark Wanderer rows). The 85-frame wait is a game-thread callback that re-queues itself until the due
// frame: no SDK service has a frame timer, and D2R's unit events take no callback.
// shortcut: the due frame is checked each update, so the boss waits for the game thread, not a unit event.
//
// The shards: a boss that dies carrying state `mapevent` in event 14 drops 4-6 Worldstone Shards (PD2: 4-10, seed % 7 + 4,
// ProjectDiablo.dll 0x10379950; the owner chose 4-6, docs/deviations.md), through a monster-death listener.
#include "wanderer.h"

#include "ai_hooks.h"
#include "corruption_roll.h"
#include "level_fx.h"
#include "log.h"
#include "map_events.h"
#include "uber_drops.h"

#include <D2RLPlugin/item.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/threads.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t DarkWandererMonster = 938;  // monstats `darkwandererMap` (PD2 921), port id
constexpr int32_t DarkWandererRow = 91;       // monai.txt `DarkWanderer`: D2R's think is d2r::sig::AiDarkWanderer
constexpr int32_t DeathDelay = 85;            // frames from the Wanderer's leave to the boss (PD2's unit timer)
constexpr int32_t DeathSound = 705;           // darkwanderer_death
constexpr int32_t BossState = 259;            // state `mapevent` (PD2 213)
constexpr int32_t StatDamagePercent = 25;     // damagepercent
constexpr int32_t StatMasteryFirst = 329;     // passive_fire_mastery .. passive_pois_mastery (329-332)
constexpr int32_t MasteryCount = 4;
constexpr int32_t BaalCrab = 544;             // monstats `baalcrab`: pool A's one boss at x1 life
constexpr int32_t MaxTries = 50;
constexpr int32_t SpotSize = 3;
constexpr int32_t SpotCount = 5;
constexpr uint32_t MaxRuns = 100000;          // game-thread runs without a due frame: give up with a warning
constexpr int32_t DarkWandererEvent = 14;     // the game's map event while the Wanderer's event runs
constexpr int32_t ShardItemLevel = 131;       // PD2's item level for the shards
constexpr int32_t ShardMin = 4;
constexpr int32_t ShardSpread = 3;            // shards = random below 3, plus 4: 4-6
static_assert(ShardMin + ShardSpread - 1 == 6);
constexpr int32_t MonstatsCountOffset = 0xf60;  // DataTables: the monstats row count (int64), read by SpawnMonsterAt

// The think (vanilla's numbers, from D2R's AiDarkWanderer)
constexpr int32_t ReachDistance = 20;   // a target nearer than this starts the leave (tick distance < 0x14)
constexpr int32_t SpotUp = 20;          // the spot is 20 north of where he is when he starts walking
constexpr int32_t AtSpot = 2;           // he has reached the spot when closer than this
constexpr int32_t MaxTrips = 2;         // walks back to the spot before he leaves anyway
constexpr int32_t IdleNoTarget = 10;    // frames to idle with no target
constexpr int32_t IdleTarget = 40;      // frames to idle with a target 20+ away
constexpr size_t SpotSlots = 8;
// D2R's walk, distance and step helpers are signature entries (d2r::sig::WalkToSpot, SpotDistance, AiStepBeforeWalk and
// AiSetModeXY, which the leave calls with mode 0: death, no kill). Argument order as the owner's disassembly notes give.

// Pool A: monstats ids of andariel, duriel, mephisto, diablo and baalcrab, the map bosses (PD2's boss set)
constexpr int32_t PoolA[] = {156, 211, 242, 243, 544};
// Pool B's superuniques by hcIdx (PD2's superunique set). Each spawns as monstats count + hcIdx (D2Common 10285).
constexpr int32_t PoolBSuperuniques[] = {6, 11, 12, 15, 18, 20, 21, 23, 24, 36, 37, 38, 39, 42, 49, 52, 65};
// Pool B's monsters, spawned as their monstats ids: Izual and Blood Raven
constexpr int32_t PoolBMonsters[] = {256, 267};
constexpr int32_t PoolBSize = static_cast<int32_t>(std::size(PoolBSuperuniques) + std::size(PoolBMonsters));

struct Scale {
	int32_t mult;   // maxhp multiplier
	int32_t bonus;  // damagepercent; the masteries get twice this
};

// PD2: pool A x2 (x1 for Baal), pool B x4; the bonus is +50 for pool A and +100 for pool B
constexpr auto Multipliers(bool poolA, int32_t id) noexcept -> Scale {
	return poolA ? Scale {id == BaalCrab ? 1 : 2, 50} : Scale {4, 100};
}
static_assert(Multipliers(true, BaalCrab).mult == 1 && Multipliers(true, BaalCrab).bonus == 50);
static_assert(Multipliers(true, 156).mult == 2 && Multipliers(true, 156).bonus == 50);
static_assert(Multipliers(false, 6).mult == 4 && Multipliers(false, 6).bonus == 100);

template <size_t N>
constexpr auto Ascending(const int32_t (&v)[N]) noexcept -> bool {
	for (size_t i = 1; i < N; ++i) {
		if (v[i - 1] >= v[i]) {
			return false;
		}
	}
	return true;
}
static_assert(std::size(PoolA) == 5 && Ascending(PoolA));
static_assert(PoolBSize == 19 && Ascending(PoolBSuperuniques) && Ascending(PoolBMonsters));
static_assert(MasteryCount == 4 && StatMasteryFirst + MasteryCount - 1 == 332);

struct Pending {
	bool used;
	uint32_t gameId;     // the game's id (game +0x08)
	uint32_t unitId;     // the Wanderer's unit id
	int32_t due;         // the game frame of the boss
	uint32_t runs;       // game-thread runs without the due frame
	int32_t x, y;        // the spot he left from
	int32_t targetType;  // the target he had at the think's start (-1 none)
	uint32_t targetId;   // its unit id
};
constexpr size_t PendingSlots = 8;

// Where he walks to: the spot is kept per unit, as his think's state is kept in the AI control's params
struct SpotRecord {
	uint32_t unitId;  // 0: free
	int32_t x, y;
};

Pending g_pending[PendingSlots] {};
SpotRecord g_spots[SpotSlots] {};
size_t g_nextSpot = 0;
// The event's boss (game id, unit id): its death drops the shards, whatever state it carries then
struct BossRecord {
	uint32_t gameId;
	uint32_t unitId;  // 0: none
};
BossRecord g_boss {};
const D2RL::PluginContext* g_context = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
std::atomic<uint32_t> g_leaves {0};
std::atomic<uint32_t> g_bosses {0};
std::atomic<uint32_t> g_shards {0};

auto GameOf(uint32_t gameId) noexcept -> Game* {
	const auto id = static_cast<uint16_t>(gameId);
	const uint64_t key = d2r::Engine<uint64_t (*)(uint16_t) noexcept>(d2r::sig::GameKey)(id);
	Game* game = key != 0 ? d2r::Engine<Game* (*)(uint64_t) noexcept>(d2r::sig::GameById)(key) : nullptr;
	return game != nullptr && At<uint16_t>(game, d2r::rva::GameIdField) == id ? game : nullptr;
}

void RunPending(const D2RL::PluginContext* context, void* user) noexcept;

auto QueueRun(size_t slot) noexcept -> bool {
	return g_threads->runOnGameThread(g_context, RunPending, reinterpret_cast<void*>(slot)) ==
	       D2RL::Threads::Result::Success;
}

// PD2's buff on the boss: max life x mult, and current life too when it was full (a deviation, deviations.md); then a
// stat list with state `mapevent`, damagepercent +bonus and the four masteries at twice that. As lucion_ai's SetShroud
// and uber_scaling, then QueueUnitUpdate (PD2's queue-update call).
void Buff(Game* game, Unit* boss, bool poolA, int32_t id, uint32_t unitId, int32_t tryNo, int32_t frame) noexcept {
	using namespace d2r::rva;
	const Scale s = Multipliers(poolA, id);
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat);
	const auto setUnit = d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::SetUnitStat);
	const int32_t maxhp = stat(boss, StatMaxLife, 0);
	const bool full = stat(boss, StatLife, 0) >= maxhp;
	const auto target = static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(maxhp) * s.mult, 0x7fffffff));
	setUnit(boss, StatMaxLife, target, 0);
	if (full) {
		setUnit(boss, StatLife, target, 0);
	}
	const auto hcId = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(d2r::sig::UnitId)(boss, __FILE__, __LINE__);
	void* list = d2r::Engine<void* (*)(int32_t, int32_t, int32_t, int32_t) noexcept>(d2r::sig::NewStatList)(
		8, 0, At<int32_t>(boss, UnitType), hcId);
	if (list == nullptr) {
		plog::Warn("wanderer: no stat list for boss %d (unit %u), max life only", id, unitId);
		return;
	}
	d2r::Engine<void (*)(void*, int32_t) noexcept>(d2r::sig::SetListState)(list, BossState);
	const auto setList = [list, flag = d2r::DataFlag(game)](int32_t st, int32_t v) noexcept {
		d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::SetListStat)(flag, list, st, v, 0);
	};
	setList(StatDamagePercent, s.bonus);
	for (int32_t m = 0; m < MasteryCount; ++m) {
		setList(StatMasteryFirst + m, 2 * s.bonus);
	}
	d2r::Engine<void (*)(Unit*, void*, int32_t) noexcept>(d2r::sig::AttachStatList)(boss, list, 1);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::SetStateBit)(boss, BossState, 1);  // AttachStatList does not (#630)
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::SetStateGfxFlag)(boss, BossState, 1);
	d2r::Engine<void (*)(Unit*) noexcept>(d2r::sig::QueueUnitUpdate)(boss);
	int32_t x = 0, y = 0;
	d2r::UnitXY(boss, &x, &y);
	plog::Info("wanderer: boss %d (pool %s) at %d,%d: maxhp %d -> %d, damage%% +%d, masteries +%d, try %d, frame %d",
	           id, poolA ? "A" : "B", x, y, maxhp >> 8, target >> 8, s.bonus, 2 * s.bonus, tryNo, frame);
	g_bosses.fetch_add(1);
}

// PD2's up to 50 tries at the spot: a refused superunique (already spawned this game) rolls again. The spot is the
// corpse's, or the saved spot when the corpse is gone (85 frames after a leave the corpse may be freed).
void SpawnBoss(Game* game, const Pending& p) noexcept {
	using namespace d2r::rva;
	Unit* corpse = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(d2r::sig::FindUnit)(game, 1, p.unitId);
	void* room = nullptr;
	int32_t xy[2] = {p.x, p.y};
	if (corpse != nullptr) {
		room = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::UnitRoom)(corpse);
		d2r::UnitXY(corpse, &xy[0], &xy[1]);
	} else {
		Unit* target = p.targetType >= 0
			? d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(d2r::sig::FindUnit)(game, p.targetType, p.targetId)
			: nullptr;
		void* near = target != nullptr ? d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::UnitRoom)(target) : nullptr;
		room = near != nullptr ? d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(d2r::sig::RoomAtXY)(near, p.x, p.y)
		                       : nullptr;
		if (room == nullptr) {
			plog::Info("wanderer: no corpse and no room near %d,%d, no boss", p.x, p.y);
			return;
		}
		plog::Info("wanderer: corpse gone, boss at the saved spot %d,%d (target %d/%u)", p.x, p.y, p.targetType, p.targetId);
	}
	void* found = nullptr;
	if (room != nullptr) {
		d2r::Engine<void (*)(Game*, void*, int32_t*, uint32_t, uint32_t, void**, uint64_t, int32_t) noexcept>(
			d2r::sig::FindFreeSpot)(game, room, xy, SpotSize, 0x400, &found, SpotCount, 100);
	}
	if (found == nullptr) {
		plog::Info("wanderer: no free spot at the spot, no boss");
		return;
	}
	const auto dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const uint8_t* tables = d2r::Engine<const uint8_t* (*)(uint8_t) noexcept>(d2r::sig::DataTables)(dataFlag);
	if (tables == nullptr) {
		plog::Warn("wanderer: no data tables, no boss");
		return;
	}
	const auto count = static_cast<int32_t>(At<int64_t>(tables, MonstatsCountOffset));
	const auto frame = At<int32_t>(game, GameFrame);
	const auto supers = static_cast<int32_t>(std::size(PoolBSuperuniques));
	for (int32_t attempt = 1; attempt <= MaxTries; ++attempt) {
		const bool poolA = RollRand() % 3 == 0;
		int32_t id = 0;
		if (poolA) {
			id = PoolA[RollRand() % static_cast<int32_t>(std::size(PoolA))];
		} else {
			const int32_t pick = RollRand() % PoolBSize;
			id = pick < supers ? count + PoolBSuperuniques[pick] : PoolBMonsters[pick - supers];
		}
		Unit* boss = d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(
			d2r::sig::SpawnMonsterAt)(game, found, id, xy[0], xy[1], 1, 0);
		if (boss != nullptr) {
			g_boss = {static_cast<uint32_t>(At<uint16_t>(game, GameIdField)),
			          static_cast<uint32_t>(d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(d2r::sig::UnitId)(boss, __FILE__, __LINE__))};
			Buff(game, boss, poolA, id, p.unitId, attempt, frame);
			return;
		}
	}
	plog::Info("wanderer: no boss after %d tries", MaxTries);
}

// Game thread: the boss at the spot once the due frame is reached; before it, queues the next run
void RunPending(const D2RL::PluginContext* context, void* user) noexcept {
	(void)context;
	const auto slot = reinterpret_cast<size_t>(user);
	if (slot >= PendingSlots || !g_pending[slot].used) {
		return;
	}
	Pending& p = g_pending[slot];
	Game* game = GameOf(p.gameId);
	if (game == nullptr) {
		p.used = false;
		plog::Info("wanderer: the game is gone, no boss");
		return;
	}
	if (At<int32_t>(game, d2r::rva::GameFrame) < p.due) {
		if (++p.runs > MaxRuns || !QueueRun(slot)) {
			p.used = false;
			plog::Warn("wanderer: no boss, the frame never reached %d (%u runs)", p.due, p.runs);
		}
		return;
	}
	const Pending due = p;
	p.used = false;
	SpawnBoss(game, due);
}

// The leave: D2R's unit flag and death mode (the think's own leave, as D2R's own leave does), then the sound and the boss 85
// frames on
void Leave(Game* game, Unit* unit, Unit* target) noexcept {
	using namespace d2r::rva;
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(d2r::sig::SetUnitFlags)(unit, UnitNoTreasure, 1);
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::AiSetModeXY)(game, unit, 0, 0, 0);
	const auto gameId = static_cast<uint32_t>(At<uint16_t>(game, GameIdField));
	const auto unitId = At<uint32_t>(unit, UnitIdField);
	int32_t x = 0, y = 0;
	d2r::UnitXY(unit, &x, &y);
	size_t slot = PendingSlots;
	for (size_t i = 0; i < PendingSlots && slot == PendingSlots; ++i) {
		// a slot whose game ended is free too: the SDK drops its queued work, so it would stay used
		if (!g_pending[i].used || GameOf(g_pending[i].gameId) == nullptr) {
			slot = i;
		}
	}
	if (slot == PendingSlots) {
		plog::Warn("wanderer: no free slot, no boss for unit %u", unitId);
		return;
	}
	PlayLevelSound(game, 0, DeathSound);
	const int32_t due = At<int32_t>(game, GameFrame) + DeathDelay;
	g_pending[slot] = {true, gameId, unitId, due, 0, x, y, target != nullptr ? At<int32_t>(target, UnitType) : -1,
	                   target != nullptr ? At<uint32_t>(target, UnitIdField) : 0u};
	++g_leaves;
	if (!QueueRun(slot)) {
		g_pending[slot].used = false;
		plog::Warn("wanderer: could not queue the boss for unit %u", unitId);
		return;
	}
	plog::Info("wanderer: left, unit %u at %d,%d (mode %d), boss due at frame %d", unitId, x, y,
	           At<int32_t>(unit, UnitMode), due);
}

// The spot record of a unit: its own if it has one, else a free or the oldest slot
SpotRecord& SpotOf(uint32_t unitId) noexcept {
	for (auto& s : g_spots) {
		if (s.unitId == unitId) {
			return s;
		}
	}
	auto& s = g_spots[g_nextSpot++ % SpotSlots];
	s.unitId = unitId;
	return s;
}

// D2R's think for the Wanderer minus the a3q7 quest gate (see the top of this file). Vanilla: no target or one 20 or
// more away idles; a near target sends him to a spot 20 north of where he is, walks there, and once within 2 of it
// (or after 2 walks back) he leaves. The first `ai control` params (state, walks) live at +0x44 and +0x48, as the
// Regurgitator's do.
void ThinkWanderer(Game* game, Unit* unit, uint8_t* tick) noexcept {
	using namespace d2r::rva;
	auto* control = tick != nullptr ? At<uint8_t*>(tick, AiTickControl) : nullptr;
	Unit* target = tick != nullptr ? At<Unit*>(tick, AiTickTarget) : nullptr;
	const int32_t distance = tick != nullptr ? At<int32_t>(tick, AiTickDistance) : 0;
	if (control == nullptr || target == nullptr || distance >= ReachDistance) {
		d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(d2r::sig::AiIdle)(game, unit, target == nullptr ? IdleNoTarget : IdleTarget);
		return;
	}
	auto* params = reinterpret_cast<int32_t*>(control + AiControlParams);
	int32_t& state = params[0];
	int32_t& trips = params[1];
	const auto unitId = At<uint32_t>(unit, UnitIdField);
	SpotRecord& spot = SpotOf(unitId);
	const auto walk = d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(d2r::sig::WalkToSpot);
	if (state == 0) {
		int32_t x = 0, y = 0;
		d2r::UnitXY(unit, &x, &y);
		spot = {unitId, x, y - SpotUp};
		state = 1;
		trips = 0;
	}
	if (state == 1) {
		walk(game, unit, spot.x, spot.y);
		state = 2;
		return;
	}
	const auto dist = d2r::Engine<uint32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::SpotDistance)(unit, spot.x, spot.y);
	if (dist < static_cast<uint32_t>(AtSpot) || trips > MaxTrips) {
		Leave(game, unit, target);
		return;
	}
	++trips;
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::AiStepBeforeWalk)(unit, 1, 0, 0);
	walk(game, unit, spot.x, spot.y);
}

// A dead boss of the event: 4-6 Worldstone Shards (PD2's quality 7 turns into normal on a corrupted item, so normal is asked)
void OnShardDeath(const D2RL::PluginContext* context, const D2RL::Lifecycle::MonsterDeathEvent* event,
                  void* userData) noexcept {
	using namespace d2r::rva;
	(void)context;
	(void)userData;
	if (event == nullptr || event->monster.type != D2RL::Lifecycle::UnitType::Monster || event->gameId > 0xffff) {
		return;
	}
	Game* game = GameOf(event->gameId);
	if (game == nullptr || MapEvent(game) != DarkWandererEvent) {
		return;
	}
	Unit* monster = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(d2r::sig::FindUnit)(game, 1, event->monster.id);
	const bool recorded = g_boss.unitId != 0 && g_boss.unitId == event->monster.id && g_boss.gameId == event->gameId;
	if (monster == nullptr ||
	    (!recorded && d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(monster, BossState) == 0) ||
	    (At<uint32_t>(monster, UnitMainFlags) & UnitNoTreasure) != 0) {
		return;
	}
	if (recorded) {
		g_boss = {};
	}
	const auto count = static_cast<int32_t>(
		d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(monster), ShardSpread)) + ShardMin;
	for (int32_t n = 0; n < count; ++n) {
		DropUniqueAt(game, monster, D2RL::Items::MakeItemCode("wss"), -1, ShardItemLevel, QualityNormal);
	}
	g_shards.fetch_add(static_cast<uint32_t>(count));
	plog::Info("wanderer: %d shards from boss %d (unit %u), event %d", count, event->monster.classId, event->monster.id,
	           DarkWandererEvent);
}

}  // namespace

auto InstallWanderer(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const D2RL::ThreadService* threads = nullptr;
	if (context->QueryService(&threads) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasThreadServiceField(threads, D2RL::ThreadServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: no thread service");
		return 0;
	}
	if (!ai_registry::RegisterAt(context, DarkWandererRow, d2r::sig::AiDarkWanderer.address, DarkWandererMonster, &ThinkWanderer)) {
		std::snprintf(report, reportSize, "off: monai %d think not swapped (the slot is not D2R's think)", DarkWandererRow);
		return 0;
	}
	g_context = context;
	g_threads = threads;
	const D2RL::LifecycleService* lifecycle = nullptr;
	const D2RL::Lifecycle::MonsterDeathListener listener {
		.structSize = D2RL::Lifecycle::MonsterDeathListenerSize, .flags = 0, .callback = OnShardDeath, .userData = nullptr};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	const bool shards = context->QueryService(&lifecycle) == D2RL::ServiceQueryResult::Success &&
	                    D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) &&
	                    lifecycle->registerMonsterDeathListener(context, &listener, &handle) == D2RL::Lifecycle::Result::Success;
	std::snprintf(report, reportSize, "a buffed boss %d frames after the Dark Wanderer leaves (monai %d think)%s",
	              DeathDelay, DarkWandererRow, shards ? ", 4-6 shards per boss" : ", no shard drop");
	return 1;
}

auto WandererCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "wanderer: %u leaves, %u bosses, %u shards", g_leaves.load(), g_bosses.load(), g_shards.load());
}
