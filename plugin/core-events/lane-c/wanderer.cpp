// The Dark Wanderer's boss (#1006), PD2's ProjectDiablo.dll 0x10375fe0 (the death step) and 0x10370d90 (the boss). PD2
// plays the death sound, then 85 frames later spawns a boss at the Wanderer's corpse: 1 in 3 from pool A (the map
// bosses), else pool B (the superuniques and Izual, Blood Raven). A boss gets its max life multiplied and a stat list
// with state `mapevent` and a damage and mastery bonus (docs/deviations.md, Dark Wanderer rows).
// The trigger is the SDK's monster-death listener (horazon.cpp). The 85-frame wait is a game-thread callback that
// re-queues itself each update until the due frame: no SDK service has a frame timer, and D2R's unit events take no
// callback. shortcut: the due frame is checked each update, so the boss waits for the game thread, not a unit event.
#include "wanderer.h"

#include "corruption_roll.h"
#include "level_fx.h"
#include "log.h"

#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/threads.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t DarkWandererMonster = 938;  // monstats `darkwandererMap` (PD2 921), port id
constexpr int32_t DeathDelay = 85;            // frames from the Wanderer's death to the boss (PD2's unit timer)
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
constexpr int32_t MonstatsCountOffset = 0xf60;  // DataTables: the monstats row count (int64), read by SpawnMonsterAt

// Pool A: monstats ids of andariel, duriel, mephisto, diablo and baalcrab, the map bosses (PD2's set 0x105dea28)
constexpr int32_t PoolA[] = {156, 211, 242, 243, 544};
// Pool B's superuniques by hcIdx (PD2's set 0x105de7dc). Each spawns as monstats count + hcIdx (D2Common 10285).
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
	uint32_t gameId;  // the game's id (game +0x08)
	uint32_t unitId;  // the dead Wanderer's unit id
	int32_t due;      // the game frame of the boss
	uint32_t runs;    // game-thread runs without the due frame
};
constexpr size_t PendingSlots = 8;

Pending g_pending[PendingSlots] {};
const D2RL::PluginContext* g_context = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
std::atomic<uint32_t> g_deaths {0};
std::atomic<uint32_t> g_bosses {0};

auto GameOf(uint32_t gameId) noexcept -> Game* {
	const auto id = static_cast<uint16_t>(gameId);
	const uint64_t key = d2r::Engine<uint64_t (*)(uint16_t) noexcept>(d2r::rva::GameKey)(id);
	Game* game = key != 0 ? d2r::Engine<Game* (*)(uint64_t) noexcept>(d2r::rva::GameById)(key) : nullptr;
	return game != nullptr && At<uint16_t>(game, d2r::rva::GameIdField) == id ? game : nullptr;
}

void RunPending(const D2RL::PluginContext* context, void* user) noexcept;

auto QueueRun(size_t slot) noexcept -> bool {
	return g_threads->runOnGameThread(g_context, RunPending, reinterpret_cast<void*>(slot)) ==
	       D2RL::Threads::Result::Success;
}

// PD2's buff on the boss: max life x mult, and current life too when it was full (a deviation, deviations.md); then a
// stat list with state `mapevent`, damagepercent +bonus and the four masteries at twice that. As lucion_ai's SetShroud
// and uber_scaling, then QueueUnitUpdate (PD2's 0x1030bd80).
void Buff(Game* game, Unit* boss, bool poolA, int32_t id, uint32_t unitId, int32_t tryNo, int32_t frame) noexcept {
	using namespace d2r::rva;
	const Scale s = Multipliers(poolA, id);
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const auto setUnit = d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat);
	const int32_t maxhp = stat(boss, StatMaxLife, 0);
	const bool full = stat(boss, StatLife, 0) >= maxhp;
	const auto target = static_cast<int32_t>(std::min<int64_t>(static_cast<int64_t>(maxhp) * s.mult, 0x7fffffff));
	setUnit(boss, StatMaxLife, target, 0);
	if (full) {
		setUnit(boss, StatLife, target, 0);
	}
	const auto hcId = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitId)(boss, __FILE__, __LINE__);
	void* list = d2r::Engine<void* (*)(int32_t, int32_t, int32_t, int32_t) noexcept>(NewStatList)(
		8, 0, At<int32_t>(boss, UnitType), hcId);
	if (list == nullptr) {
		plog::Warn("wanderer: no stat list for boss %d (unit %u), max life only", id, unitId);
		return;
	}
	d2r::Engine<void (*)(void*, int32_t) noexcept>(SetListState)(list, BossState);
	const auto setList = [list, flag = d2r::DataFlag(game)](int32_t st, int32_t v) noexcept {
		d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat)(flag, list, st, v, 0);
	};
	setList(StatDamagePercent, s.bonus);
	for (int32_t m = 0; m < MasteryCount; ++m) {
		setList(StatMasteryFirst + m, 2 * s.bonus);
	}
	d2r::Engine<void (*)(Unit*, void*, int32_t) noexcept>(AttachStatList)(boss, list, 1);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateBit)(boss, BossState, 1);  // AttachStatList does not (#630)
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateGfxFlag)(boss, BossState, 1);
	d2r::Engine<void (*)(Unit*) noexcept>(QueueUnitUpdate)(boss);
	int32_t x = 0, y = 0;
	d2r::UnitXY(boss, &x, &y);
	plog::Info("wanderer: boss %d (pool %s) at %d,%d: maxhp %d -> %d, damage%% +%d, masteries +%d, try %d, frame %d",
	           id, poolA ? "A" : "B", x, y, maxhp >> 8, target >> 8, s.bonus, 2 * s.bonus, tryNo, frame);
	g_bosses.fetch_add(1);
}

// PD2's up to 50 tries at the corpse's free spot: a refused superunique (already spawned this game) rolls again
void SpawnBoss(Game* game, uint32_t unitId) noexcept {
	using namespace d2r::rva;
	Unit* corpse = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(FindUnit)(game, 1, unitId);
	if (corpse == nullptr) {
		plog::Info("wanderer: no corpse for unit %u, no boss", unitId);
		return;
	}
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(corpse);
	int32_t xy[2] = {0, 0};
	d2r::UnitXY(corpse, &xy[0], &xy[1]);
	void* found = nullptr;
	if (room != nullptr) {
		d2r::Engine<void (*)(Game*, void*, int32_t*, uint32_t, uint32_t, void**, uint64_t, int32_t) noexcept>(
			FindFreeSpot)(game, room, xy, SpotSize, 0x400, &found, SpotCount, 100);
	}
	if (found == nullptr) {
		plog::Info("wanderer: no free spot at the corpse, no boss");
		return;
	}
	const auto dataFlag = static_cast<uint8_t>(d2r::DataFlag(game));
	const uint8_t* tables = d2r::Engine<const uint8_t* (*)(uint8_t) noexcept>(DataTablesOf)(dataFlag);
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
			SpawnMonsterAt)(game, found, id, xy[0], xy[1], 1, 0);
		if (boss != nullptr) {
			Buff(game, boss, poolA, id, unitId, attempt, frame);
			return;
		}
	}
	plog::Info("wanderer: no boss after %d tries", MaxTries);
}

// Game thread: the boss at the corpse once the due frame is reached; before it, queues the next run
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
	p.used = false;
	SpawnBoss(game, p.unitId);
}

void OnMonsterDeath(const D2RL::PluginContext* context, const D2RL::Lifecycle::MonsterDeathEvent* event,
                    void* userData) noexcept {
	using D2RL::Lifecycle::UnitType;
	(void)context;
	(void)userData;
	if (event == nullptr || event->monster.type != UnitType::Monster || event->monster.classId != DarkWandererMonster ||
	    event->gameId > 0xffff) {
		return;
	}
	Game* game = GameOf(event->gameId);
	if (game == nullptr) {
		return;
	}
	size_t slot = PendingSlots;
	for (size_t i = 0; i < PendingSlots && slot == PendingSlots; ++i) {
		// a slot whose game ended is free too: the SDK drops its queued work, so it would stay used
		if (!g_pending[i].used || GameOf(g_pending[i].gameId) == nullptr) {
			slot = i;
		}
	}
	if (slot == PendingSlots) {
		plog::Warn("wanderer: no free slot, no boss for unit %u", event->monster.id);
		return;
	}
	PlayLevelSound(game, 0, DeathSound);
	const int32_t due = At<int32_t>(game, d2r::rva::GameFrame) + DeathDelay;
	g_pending[slot] = {true, event->gameId, event->monster.id, due, 0};
	++g_deaths;
	if (!QueueRun(slot)) {
		g_pending[slot].used = false;
		plog::Warn("wanderer: could not queue the boss for unit %u", event->monster.id);
		return;
	}
	plog::Info("wanderer: died, unit %u, boss due at frame %d", event->monster.id, due);
}

}  // namespace

auto InstallWanderer(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: no lifecycle service");
		return 0;
	}
	const D2RL::ThreadService* threads = nullptr;
	if (context->QueryService(&threads) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasThreadServiceField(threads, D2RL::ThreadServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: no thread service");
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
	g_threads = threads;
	std::snprintf(report, reportSize, "a buffed boss %d frames after the Dark Wanderer dies", DeathDelay);
	return 1;
}

auto WandererCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "wanderer: %u deaths, %u bosses", g_deaths.load(), g_bosses.load());
}
