// The Treasure Fallen's drops (#1153, docs/research/map-events-audit.md section 5). PD2's Fallen think (0x1035a3c0) drops
// the monster's treasure class once for each 10% of life lost while it is hit (n counts them), and its death step
// (0x10378d10) drops the rest, 9 - n times, so the stock death drop makes it 10 rolls in all. D2R does neither: one roll.
//
// Think: class 962 (treasurefallenMap) on monai row 6 runs the per-10% drop, then D2R's own Fallen think. PD2's flee
// from the player is not ported (docs/deviations.md). D2R's think keeps its own state in AI param 0, so n lives in a small
// table here, per game and unit. Death: a monster-death listener drops 9 - n more and clears the slot.
// Both drops go through D2R's MonsterTcDrop. PD2 passes no killer; D2R's drop reads one for the quality roll, so the
// killer is a player (the tick's target or the nearest one) and no player means no drop.
// D2R's Fallen think is found by a signature of its own (an entry here, not a d2r::sig one); the part is off without it.
#include "treasure_fallen.h"

#include "ai_common.h"
#include "ai_hooks.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"

#include <D2RLPlugin/lifecycle_events.h>

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t TreasureFallenMonster = 962;  // monstats `treasurefallenMap` (PD2 945), port id
constexpr int32_t FallenRow = 6;                // monai.txt `Fallen`
constexpr int32_t Rolls = 10;                   // PD2: 9 drops while hit or on death, plus the stock death drop
constexpr size_t Slots = 16;                    // at most 3 fallen a map

// 93847: 0x5ae650, monai row 6's think (reference only), read from the AI table
sigscan::Entry AiFallen {"AiFallen", "ai", sigscan::Kind::Code, 0, 0,
                         "4c 89 44 24 18 55 56 41 55 41 56 48 81 ec 88 00 00 00 48 8b e9 49 8b f0 48 8b ca 4c 8b f2 e8 ?? ?? ?? ??"};
sigscan::Entry* const Own[] = {&AiFallen};

struct Count {
	bool used;
	uint16_t gameId;
	uint32_t unitId;
	int32_t n;  // drops made while it was hit
	uint32_t seq;  // order of creation, the oldest is overwritten when the table is full
};
Count g_counts[Slots];  // the game thread runs both the think and the death listener
uint32_t g_seq = 0;
const Game* g_game = nullptr;  // the game the slots belong to
uint16_t g_gameId = 0;
int32_t g_frame = 0;
std::atomic<uint32_t> g_hitDrops {0}, g_deathDrops {0};

auto CountOf(uint16_t gameId, uint32_t unitId, bool add) noexcept -> Count* {
	Count* slot = &g_counts[0];  // a free slot, else the oldest
	for (Count& c : g_counts) {
		if (c.used && c.gameId == gameId && c.unitId == unitId) {
			return &c;
		}
		slot = (slot->used && (!c.used || c.seq < slot->seq)) ? &c : slot;
	}
	if (add) {
		*slot = {true, gameId, unitId, 0, ++g_seq};
		return slot;
	}
	return nullptr;
}

// One roll of the unit's treasure class (PD2 0x103843d0); false when nothing was rolled
auto Drop(Game* game, Unit* unit, Unit* killer) noexcept -> bool {
	using namespace d2r::rva;
	if (killer == nullptr || (At<uint32_t>(unit, UnitMainFlags) & UnitNoTreasure) != 0) {
		return false;
	}
	struct {
		void* unused;
		Unit* monster;
		Unit* killer;
	} drop {nullptr, unit, killer};
	d2r::Engine<void (*)(Game*, Unit*, int32_t, void*) noexcept>(d2r::sig::MonsterTcDrop)(game, unit, 0, &drop);
	return true;
}

// Game ids are reused and unit ids restart, so a fallen alive when a game ends must not leave its count behind. D2RLoader
// drops queued game-thread callbacks when the session changes, so the think itself spots a new game: another Game, another
// id, or a frame counter that went back. shortcut: a new game at the same address and id whose frame is still past the
// last think's is missed; upgrade when the SDK has a per-game value.
void ClearCounts() noexcept {
	for (Count& c : g_counts) {
		c = {};
	}
	plog::Info("treasure fallen: counts cleared for new game");
}

void CheckGame(Game* game) noexcept {
	const auto id = At<uint16_t>(game, d2r::rva::GameIdField);
	const auto frame = At<int32_t>(game, d2r::rva::GameFrame);
	if (game != g_game || id != g_gameId || frame < g_frame) {
		ClearCounts();
		g_game = game;
		g_gameId = id;
	}
	g_frame = frame;
}

void Think(Game* game, Unit* unit, uint8_t* tick) noexcept {
	using namespace d2r::rva;
	CheckGame(game);
	const auto life = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat)(unit, StatLife, 0);
	const int64_t max = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::UnitMaxLife)(unit);
	if (max > 0) {
		const auto pct = static_cast<int32_t>(static_cast<int64_t>(life) * 100 / max);
		Count* c = CountOf(At<uint16_t>(game, GameIdField), At<uint32_t>(unit, UnitIdField), true);
		if (c != nullptr && c->n < 10 - (pct + 9) / 10) {  // PD2: n < 10 - ceil(pct / 10)
			Unit* target = tick != nullptr ? At<Unit*>(tick, AiTickTarget) : nullptr;
			Unit* killer = target != nullptr && At<int32_t>(target, d2r::rva::UnitType) == 0
			                   ? target : ai_common::NearestPlayer(game, unit, ai_common::LevelOf(unit));
			if (Drop(game, unit, killer)) {  // no player: nothing is counted, the next tick tries again
				++c->n;
				g_hitDrops.fetch_add(1);
				plog::Info("treasure fallen: hit drop %d of 9 at %d%% life (unit %u)", c->n, pct, At<uint32_t>(unit, UnitIdField));
			}
		}
	}
	ai_registry::Vanilla(FallenRow)(game, unit, tick);
}

void OnDeath(const D2RL::PluginContext*, const D2RL::Lifecycle::MonsterDeathEvent* event, void*) noexcept {
	using namespace d2r::rva;
	using D2RL::Lifecycle::UnitType;
	if (event == nullptr || event->monster.type != UnitType::Monster ||
	    event->monster.classId != TreasureFallenMonster || event->gameId > 0xffff) {
		return;
	}
	const auto id = static_cast<uint16_t>(event->gameId);
	const uint64_t key = d2r::Engine<uint64_t (*)(uint16_t) noexcept>(d2r::sig::GameKey)(id);
	Game* game = key != 0 ? d2r::Engine<Game* (*)(uint64_t) noexcept>(d2r::sig::GameById)(key) : nullptr;
	if (game == nullptr || At<uint16_t>(game, GameIdField) != id) {
		return;
	}
	CheckGame(game);  // a death before any think in a new game must not match a leftover slot
	const auto find = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(d2r::sig::FindUnit);
	Unit* monster = find(game, 1, event->monster.id);
	Count* c = CountOf(id, event->monster.id, false);
	const int32_t n = c != nullptr ? c->n : 0;
	if (c != nullptr) {
		*c = {};
	}
	if (monster == nullptr) {
		return;
	}
	// The killer, a missile counting as its owner (as uber_deaths.cpp); a non-player or none: the nearest player
	Unit* killer = event->killer.type != UnitType::Invalid
	                   ? find(game, static_cast<int32_t>(event->killer.type), event->killer.id) : nullptr;
	if (killer != nullptr && event->killer.type == UnitType::Missile) {
		killer = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, killer);
	}
	if (killer == nullptr || At<int32_t>(killer, d2r::rva::UnitType) != 0) {
		killer = ai_common::NearestPlayer(game, monster, static_cast<int32_t>(event->levelId));
	}
	int32_t dropped = 0;
	for (int32_t i = n; i < Rolls - 1; ++i) {
		dropped += Drop(game, monster, killer) ? 1 : 0;
	}
	g_deathDrops.fetch_add(static_cast<uint32_t>(dropped));
	plog::Info("treasure fallen: death drops %d (n %d)", dropped, n);
}

}  // namespace

auto InstallTreasureFallen(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	sigscan::Resolve(Own, 1, context->exeBase, nullptr, 0);
	if (AiFallen.state != sigscan::State::Ok) {
		std::snprintf(report, reportSize, "off: signature AiFallen %s", sigscan::StateText(AiFallen.state));
		return 0;
	}
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		std::snprintf(report, reportSize, "off: no lifecycle service");
		return 0;
	}
	if (!ai_registry::RegisterAt(context, FallenRow, AiFallen.address, TreasureFallenMonster, &Think)) {
		std::snprintf(report, reportSize, "off: monai %d think not swapped (the slot is not D2R's think)", FallenRow);
		return 0;
	}
	const D2RL::Lifecycle::MonsterDeathListener listener {
		.structSize = D2RL::Lifecycle::MonsterDeathListenerSize, .flags = 0, .callback = OnDeath, .userData = nullptr};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	if (lifecycle->registerMonsterDeathListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
		std::snprintf(report, reportSize, "off: monster-death listener refused");
		return 0;
	}
	std::snprintf(report, reportSize, "Treasure Fallen: a drop per 10%% of life lost, the rest on death (monai %d think)", FallenRow);
	return 1;
}

auto TreasureFallenCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "treasure fallen: %u hit drops, %u death drops", g_hitDrops.load(), g_deathDrops.load());
}
