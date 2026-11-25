// Shared surface of core-rules' per-rule skill files (plugin/core-rules/skill/*.cpp): rules other core-rules code calls
// into (item_rules.cpp's HitEvent relay). The installs register themselves (registry.h), so they are not declared here.
#pragma once

#include <D2RLPlugin/api.h>

#include "log.h"
#include "pd2_engine.h"
#include "sig.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace d2r {
struct Game;
struct Unit;
}

// --- PD2's pet leash (summon_ai.cpp's ShadowMaster calls, #1790; pet_leash.cpp's pre-think, #1989) ---
// ProjectDiablo.dll's two leashes (0x10360d40 for the ShadowMaster think, 0x103606e0 for every player pet before its
// think) share one rule: d2 = owner-to-pet squared distance, `fighting` = the pet has a target.
namespace leash {

// D2R's mover kinds (0x5bc940's fourth argument, as D2R's leash 0x5bd1e0 picks them)
constexpr int32_t MoveNone = 0, MoveBack = 1, MoveWarp = 3, MoveWander = 4, MoveAside = 5;

constexpr auto Move(int64_t d2, bool fighting, bool wanderRoll) noexcept -> int32_t {
	const double limit = (fighting ? 3.2 : 1.0) * 361.0;  // the DLL's doubles 1.0 / 3.2 times 361.0
	if (static_cast<double>(d2) > limit) {
		return d2 >= 0x900 ? MoveWarp : MoveBack;
	}
	if (fighting) {
		return MoveNone;
	}
	if (d2 <= 5) {
		return MoveAside;
	}
	return wanderRoll ? MoveWander : MoveNone;  // PD2: one think in 12
}
static_assert(Move(361, false, false) == MoveNone && Move(362, false, false) == MoveBack);
static_assert(Move(1155, true, false) == MoveNone && Move(1156, true, false) == MoveBack);
static_assert(Move(0x900, true, false) == MoveWarp && Move(5, false, false) == MoveAside);
static_assert(Move(100, false, true) == MoveWander && Move(100, true, true) == MoveNone);

// Both PD2 leashes walk back through 0x103569d0 in mode 15 (run) when the pet's monstats2 modes (+0xf0) have bit 15
// (mRN), else mode 2 (walk). D2R's mover runs kind 1 when its fifth argument is non-zero (0x5bc940's run calls).
// The AI tick holds the pet's monstats2 record at +0x30.
constexpr int TickMonStats2 = 0x30, Mon2Modes = 0xf0;
constexpr uint32_t ModeRun = 1u << 15;  // D2R's mode bit table 0x1d996d0 holds 1 << mode
constexpr auto RunArg(int32_t move, uint32_t modes) noexcept -> int32_t {
	return move == MoveBack && (modes & ModeRun) != 0 ? 1 : 0;
}
static_assert(RunArg(MoveBack, ModeRun | 4) == 1 && RunArg(MoveBack, 4) == 0 && RunArg(MoveWarp, ModeRun) == 0);
inline auto TickModes(const void* tick) noexcept -> uint32_t {
	const uint8_t* monstats2 = tick != nullptr ? d2r::At<const uint8_t*>(tick, TickMonStats2) : nullptr;
	return monstats2 != nullptr ? d2r::At<uint32_t>(monstats2, Mon2Modes) : 0;
}

using MoverFn = uint64_t (*)(d2r::Game*, d2r::Unit* owner, d2r::Unit* pet, int32_t kind, int32_t run, int32_t, uint8_t radius) noexcept;
using PathTargetFn = d2r::Unit* (*)(d2r::Game*, d2r::Unit*) noexcept;
using IsDeadFn = int32_t (*)(d2r::Unit*) noexcept;
// D2R's AI target search (0x595f80, 1.13c +0xb1bc0): (game, unit, out distance block, out flag block); it probes a path
// to the pick (restoring the path's target, point and type), as in PD2
using SearchFn = d2r::Unit* (*)(d2r::Game*, d2r::Unit*, void*, void*) noexcept;
struct Engine {
	MoverFn mover = nullptr;
	PathTargetFn pathTarget = nullptr;
	IsDeadFn isDead = nullptr;
	SearchFn search = nullptr;  // pre-think leash only (PD2 0x103606e0); the ShadowMaster leash has none
};

// D2R's leash 0x5bd1e0: its one mover call (rcx game, rdx owner, r8 pet, r9d kind, two zeros, the radius byte); E8 at +19
inline constexpr const char* MoverCallPattern = "40 88 6C 24 30 4D 8B C6 89 7C 24 28 48 8B D6 89 7C 24 20 E8";
// The ShadowMaster think's calls of the path-target getter (E8 at +6) and of the dead test (E8 at +3). The byte after
// the dead test's `test` is summon_ai's "no owner target" patch (75 -> eb), so either order of installs finds it.
inline constexpr const char* PathTargetCallPattern = "48 8B D7 49 8B CD E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 75 12";
inline constexpr const char* IsDeadCallPattern = "48 8B C8 E8 ?? ?? ?? ?? 85 C0 ?? 17 4C 8B C3 48 8B D7 49 8B CC E8";

// Fills `engine` from the three call sites; false (engine untouched) when one is missing
inline auto Find(uint64_t exeBase, Engine* engine) noexcept -> bool {
	const char* patterns[] = {MoverCallPattern, PathTargetCallPattern, IsDeadCallPattern};
	const int calls[] = {19, 6, 3};
	uint64_t at[3] = {};
	for (int i = 0; i < 3; ++i) {
		const int n = sigscan::ScanUnique(exeBase, patterns[i], calls[i], &at[i]);
		if (n != 1) {
			plog::Warn("leash: call site %d: %d matches", i, n);
			return false;
		}
		int32_t rel = 0;
		std::memcpy(&rel, reinterpret_cast<const void*>(exeBase + at[i] + 1), sizeof(rel));
		at[i] = exeBase + at[i] + 5 + static_cast<int64_t>(rel);
	}
	*engine = {reinterpret_cast<MoverFn>(at[0]), reinterpret_cast<PathTargetFn>(at[1]), reinterpret_cast<IsDeadFn>(at[2])};
	return true;
}

inline auto Dist2(d2r::Unit* a, d2r::Unit* b) noexcept -> int64_t {
	int32_t ax = 0, ay = 0, bx = 0, by = 0;
	if (!d2r::UnitXY(a, &ax, &ay) || !d2r::UnitXY(b, &bx, &by)) {
		return -1;
	}
	return int64_t {ax - bx} * (ax - bx) + int64_t {ay - by} * (ay - by);
}

// PD2's leash: non-zero once the pet moved. `moves` counts each kind (6 entries); `lastD2`, `lastMove` and
// `lastFighting` get the distance, the kind picked and whether the 3.2 limit applied. `modes`: the pet's monstats2 modes
// (TickModes), for PD2's run back.
inline auto Leash(const Engine& engine, d2r::Game* game, d2r::Unit* pet, d2r::Unit* target, d2r::Unit* owner, int32_t radius,
                  uint32_t modes, std::atomic<uint32_t>* moves, int64_t* lastD2 = nullptr, int32_t* lastMove = nullptr,
                  bool* lastFighting = nullptr) noexcept -> uint64_t {
	// PD2 leashes only a player's pet
	if (game == nullptr || pet == nullptr || owner == nullptr || d2r::At<int32_t>(owner, d2r::rva::UnitType) != 0) {
		return 0;
	}
	const int64_t d2 = Dist2(owner, pet);
	if (lastD2 != nullptr) {
		*lastD2 = d2;
	}
	if (d2 < 0) {
		return 0;
	}
	bool fighting = target != nullptr;
	if (!fighting) {
		// PD2: the unit the pet's path is after, else (pre-think only) the AI target search's pick; a live player or
		// monster within 34 subtiles. shortcut: 1155 is measured pet to that unit; PD2 measures the pet to its path's
		// target point (skipped at 0,0). Upgrade if pets drop targets they should keep.
		d2r::Unit* chase = engine.pathTarget(game, pet);
		if (chase == nullptr && engine.search != nullptr) {
			uint8_t distance[0x40] = {}, flags[0x40] = {};
			chase = engine.search(game, pet, distance, flags);
		}
		fighting = chase != nullptr && chase != owner && d2r::At<int32_t>(chase, d2r::rva::UnitType) <= 1 &&
		           engine.isDead(chase) == 0 && Dist2(pet, chase) >= 0 && Dist2(pet, chase) <= 1155;
	}
	uint32_t* seed = d2r::api.unitSeed != nullptr ? d2r::api.unitSeed(pet) : nullptr;
	const bool wander = seed != nullptr && d2r::RandomBelow(seed, 12) == 0;
	const int32_t move = Move(d2, fighting, wander);
	if (lastFighting != nullptr) {
		*lastFighting = fighting;
	}
	++moves[move];
	if (lastMove != nullptr) {
		*lastMove = move;
	}
	if (move == MoveNone) {
		return 0;
	}
	// shortcut: PD2 walks back with its own mover (0x10356a40) and measures to the owner's path end while the owner
	// moves; D2R's mover kinds and the owner's position stand in (running back as PD2 does). Upgrade if pets still lag
	// a running owner.
	return engine.mover(game, owner, pet, move, RunArg(move, modes), 0, static_cast<uint8_t>(move == MoveWarp ? 0 : radius));
}

}  // namespace leash

// --- PD2's to-hit chance (to_hit.cpp's server roll #2010, sheet_to_hit.cpp's character sheet #2048) ---
// ProjectDiablo.dll 0x10304e70 (defense, AR, defender level, attacker level) -> 5-100, step by step (32-bit integer
// math, each division truncated as idiv does)
inline constexpr auto Pd2Chance(int32_t def, int32_t ar, int32_t dlvl, int32_t alvl) noexcept -> int32_t {
	// a negative side moves onto the other one (D2R's inputs are already moved, so this is a no-op here)
	if (def < 0) {
		ar -= def;
		def = 0;
	}
	if (ar < 0) {
		def -= ar;
		ar = 0;
	}
	const int32_t sum = ar + def;
	const int32_t ratio = sum != 0 ? 111 * ar / sum : 100;
	const int32_t levels = dlvl + alvl;
	if (levels == 0) {
		return 5;
	}
	const int32_t chance = ratio * alvl * 2 / levels;
	if (chance < 5) {
		return 5;
	}
	if (chance <= 95 || alvl == 0) {
		return chance < 100 ? chance : 100;
	}
	// the ratio's part above the one that gives 95 is scaled by 90/111 (through sum, as PD2 does), then the level factor
	int32_t over = ratio - 95 * levels / (2 * alvl);
	if (sum != 0) {
		over = sum * over / 111 * 90 / sum;
	}
	const int32_t soft = over * alvl * 2 / levels + 95;
	return soft < 100 ? soft : 100;
}

// --- PD2's leech reductions (#2009, leech.cpp) ---
// Missile and area hits halve life and mana leech; a Leap Attack hit divides life leech by 3.
// Names the victim of the one Leap Attack hit about to run (skill_functions.cpp LeapHitOne); nullptrs after it.
auto LeapAttackLeech(d2r::Unit* unit, d2r::Unit* victim) noexcept -> void;

// Inventory gold cap (#2027, gold_cap.cpp): the cap function's RVA (D2R's max inventory gold of a unit, `int32_t (*)(Unit*)`), 0 when its signature is not found.
auto GoldCapRva(const D2RL::PluginContext* context) noexcept -> uint64_t;

// --- Slow skips the prime evils (#2004, slow_prime_evil.cpp) ---
// Hides the attacker's item_slow event nodes for one event 5/6 dispatch (item_rules.cpp's HitEvent relay) when the
// target is a prime evil, and shows them again for a splash hit on another unit inside an outer call that hid them.
struct SlowNodes {
	static constexpr int Max = 16;  // a unit wears 12 items; more only guards a corrupt chain
	uint8_t* node[Max];
	uint16_t restore[Max];
	int count = 0;
	int32_t outer = -1;
};
auto SlowPrimeEvilBegin(d2r::Game* game, int32_t event, d2r::Unit* unit, d2r::Unit* target, SlowNodes& nodes) noexcept -> void;
auto SlowPrimeEvilEnd(const SlowNodes& nodes) noexcept -> void;

// --- Sacrifice's missile kills (#2043, sacrifice_missile.cpp) ---
// The missile of the missile damage or AreaApply call running on this thread (D2R passes the deal its owner), or null.
auto SacrificeKillMissile() noexcept -> d2r::Unit*;

// --- D2R's corpse test without monstats2 `soft` (grim_ward_corpse.cpp #2019, hunger_corpse.cpp #2067) ---
// 0x33a5c0, the corpse test without bit 12 (#10678); 0x33a510 differs only in its tail (call 0x34fa60, not mov eax,1)
inline constexpr const char* CorpseSelPattern =
    "40 57 48 83 ec 20 48 8b f9 48 85 c9 74 6e e8 ?? ?? ?? ?? 83 f8 01 75 64 48 8b cf e8 ?? ?? ?? ?? 83 f8 0c 75 57 "
    "48 8b cf e8 ?? ?? ?? ?? 85 c0 75 4b 41 b8 cb 0e 00 00 48 89 5c 24 38 48 8d 15 ?? ?? ?? ?? 48 8b cf e8 ?? ?? ?? ?? "
    "48 8b cf 8b d8 e8 ?? ?? ?? ?? 8b d3 0f b6 c8 e8 ?? ?? ?? ?? 48 8b 5c 24 38 48 85 c0 75 1b 48 8d 4c 24 30 88 44 "
    "24 30 e8 ?? ?? ?? ?? 84 c0 74 01 cc 33 c0 48 83 c4 20 5f c3 0f b6 40 04 23 05 ?? ?? ?? ?? 74 ec b8 01 00 00 00";

// --- Shared by the per-rule skill files (#2044): helpers, constants and counters that more than one rule uses ---
// The definitions live in the rule file that owns them (functions and counters) or here (constants and types).
#include "skill_router.h"
#include "../missile_functions.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <span>

namespace pd2r::skills {

using d2r::At;
using d2r::Game;
using d2r::Unit;

using CltDoFn = int32_t (*)(Unit* unit, int32_t skillId, int32_t level) noexcept;


extern std::atomic<uint32_t> g_splitThrowCasts;

extern std::atomic<uint32_t> g_splitThrowClientCasts;

auto ClampCount(int32_t count) noexcept -> int32_t;

// --- Double Throw (PD2 srvdofunc 154 -> 183, cltdofunc 98 -> 116) ------------------------------------------------
extern std::atomic<uint32_t> g_doubleThrowCasts;

extern std::atomic<uint32_t> g_doubleThrowClientCasts;

// The unit's list on `state` dropped, state off and list freed, as RemoveSkillState (0x4370b0) does per state. That
// one removes by states.txt group and does nothing for a state with none (temp_skill_buff, the progressive states).
void DropStateList(uint32_t dataFlag, Unit* unit, int32_t state) noexcept;

// --- Split Throw's start buff (PD2 srvstfunc 79)---------------------------------------------------------------------
extern std::atomic<uint32_t> g_tempBuffs;

// --- W10 Barbarian group (docs/research/skills/w10-barbarian.md) ----------------------------------------------------------
extern std::atomic<uint32_t> g_finds;

extern std::atomic<uint32_t> g_findsRefused;

extern std::atomic<uint32_t> g_bashHits;

extern std::atomic<uint32_t> g_bashKnockbacks;

extern std::atomic<uint32_t> g_splashStarts;

// D2R's own function in table `table` slot `slot`.
template <typename Fn>
auto VanillaSlot(uint64_t table, int slot) noexcept -> Fn {
	return *reinterpret_cast<Fn*>(d2r::imageBase + table + static_cast<uint64_t>(slot) * 8);
}

// PD2's Find guard: a corpse whose owner is a player (a summon or minion) can't be searched. D2R's unowned units
// carry id 0 or -1 here; both pass.

// PD2's Find guard: a corpse whose owner is a player (a summon or minion) can't be searched. D2R's unowned units
// carry id 0 or -1 here; both pass.
inline auto OwnedByPlayer(Unit* target) noexcept -> bool {
	const uint32_t id = At<uint32_t>(target, d2r::rva::UnitOwnerId);
	return At<int32_t>(target, d2r::rva::UnitOwnerType) == 0 && id != 0 && id != UINT32_MAX;
}

// PD2's 46 bosses whose corpses can't be searched (map, uber and PD2-only-area bosses; docs/research/skills/w10-barbarian.md),
// as merged monstats ids.

struct StatValue {
	int32_t stat, value;
};

// Stats on a `state` list `owner` puts on `unit` for `frames`, by NewStateList's recast rule (PD2's timed-state
// helper 0x1036d7d0 has the same one): an older list of the same skill and level is extended and keeps its values, a
// lower level is refused, anything else is replaced. Returns what happened, for the logs.

// Stats on a `state` list `owner` puts on `unit` for `frames`, by NewStateList's recast rule (PD2's timed-state
// helper 0x1036d7d0 has the same one): an older list of the same skill and level is extended and keeps its values, a
// lower level is refused, anything else is replaced. Returns what happened, for the logs.
inline auto TimedStatList(uint32_t dataFlag, Unit* owner, Unit* unit, int32_t skillId, int32_t level, int32_t frames,
                   int32_t state, const StatValue* first, const StatValue* last) noexcept -> const char* {
	using namespace d2r::rva;
	void* old = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, state);
	const auto listInt = [](uint64_t fn, void* list) { return d2r::Engine<int32_t (*)(void*) noexcept>(fn)(list); };
	const bool extends = old != nullptr && listInt(ListSkill, old) == skillId && listInt(ListLevel, old) == level;
	d2r::StateListRequest request {owner, unit, skillId, level, frames, -1, 0, state,
	                               d2r::Engine<void (*)()>(SkillStateExpired)};
	void* list = d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	if (list == nullptr) {
		return "refused";
	}
	if (extends) {
		return "extended";
	}
	for (const StatValue* s = first; s != last; ++s) {
		d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat)(dataFlag, list, s->stat,
		                                                                                          s->value, 0);
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateGfxFlag)(unit, state, 1);
	return "new";
}


inline auto TimedStatList(uint32_t dataFlag, Unit* owner, Unit* unit, int32_t skillId, int32_t level, int32_t frames,
                   int32_t state, std::initializer_list<StatValue> stats) noexcept -> const char* {
	return TimedStatList(dataFlag, owner, unit, skillId, level, frames, state, stats.begin(), stats.end());
}

// A skill missile's params from (x, y), owned by and launched from the unit; the caller sets the targets.

// A skill missile's params from (x, y), owned by and launched from the unit; the caller sets the targets.
inline auto SkillMissileParams(Unit* unit, int32_t flags, int32_t missile, int32_t x, int32_t y, int32_t skillId,
                        int32_t level) noexcept -> d2r::MissileParams {
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = flags;
	params.owner = unit;
	params.origin = unit;
	params.missile = missile;
	params.x = x;
	params.y = y;
	params.skill = skillId;
	params.skillLevel = level;
	return params;
}

// PD2's ring (0x1036d250 server, 0x1036d370 client): one missile at every (64 / count)-th of D2's 64 directions, so
// count 10 makes 11 and count 6 makes 7; 0 or over 64 makes 64.
// RingStep: one at every step-th direction (1.13c's ring helpers, D2Game 0x93d60, take the step).

// PD2's ring (0x1036d250 server, 0x1036d370 client): one missile at every (64 / count)-th of D2's 64 directions, so
// count 10 makes 11 and count 6 makes 7; 0 or over 64 makes 64.
// RingStep: one at every step-th direction (1.13c's ring helpers, D2Game 0x93d60, take the step).
template <typename Create>
auto RingStep(int32_t step, Create create) noexcept -> int32_t {
	const auto* dx = reinterpret_cast<const int32_t*>(d2r::imageBase + d2r::rva::DirectionX);
	const auto* dy = reinterpret_cast<const int32_t*>(d2r::imageBase + d2r::rva::DirectionY);
	int32_t created = 0;
	for (int32_t i = 0; i < 64; i += step) {
		created += create(dx[i], dy[i]) ? 1 : 0;
	}
	return created;
}


template <typename Create>
auto Ring(int32_t count, Create create) noexcept -> int32_t {
	if (count <= 0 || count > 64) {
		count = 64;
	}
	return RingStep(64 / count, create);
}

// PD2's server ring call (0x1036d250 with flags 3): `count` missiles of row `missile` from x/y (Ring), owned by and
// launched from the unit. Returns how many were created.

// PD2's server ring call (0x1036d250 with flags 3): `count` missiles of row `missile` from x/y (Ring), owned by and
// launched from the unit. Returns how many were created.
inline auto ServerRing(Game* game, Unit* unit, int32_t missile, int32_t x, int32_t y, int32_t count, int32_t skillId,
                int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, missile, x, y, skillId, level);
	return Ring(count, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		return d2r::api.createMissile(game, &params) != nullptr;
	});
}

// --- Teleport (PD2 srvdofunc 155 -> router 190; docs/research/skills/sorceress-w26.md) -----------------------------------
// Read from ProjectDiablo.dll: srvdo 155 at 0x103b16d0, its teleport core at 0x1036cd20 (shared with Blood Warp).

// PD2's teleport core: refused under disable_tele, else D2R's Teleport (srvdo 27), which
// makes the core's level checks (levels.txt Teleport 0 refuses, 2 checks collision at the point).
// PD2's Hell-only level list (0x105de86c) is never filled, so it is left out.
// shortcut: PD2's "TeleportBlocked" chat line on a Teleport 0 refusal is left out: no ported level has Teleport 0.
// Upgrade when one does.
auto TeleportCore(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> bool;

// --- Blood Warp (PD2 srvdofunc 157 -> router 190, srvstfunc 66 -> 80, cltstfunc 56 -> 63) --------------------------
// Read from ProjectDiablo.dll: srvdo 157 at 0x103b1aa0, srvst 66 at 0x103b6460, cltst 56 at 0x103acda0
// (docs/research/skills/w12-blood-warp.md). The cast costs life and never kills; the cost comes back as hpregen.
extern std::atomic<uint32_t> g_bloodWarps;

extern std::atomic<uint32_t> g_bloodWarpsRefused;

// --- Desecrate (PD2 srvdofunc 161 -> router 190, cltdofunc 101 -> 121) -------------------------------------------
// Read from ProjectDiablo.dll: srvdo 161 at 0x103b1fa0, cltdo 101 at 0x103ab630 (docs/research/skills/necromancer-w13.md).
extern std::atomic<uint32_t> g_desecrates;

extern std::atomic<uint32_t> g_desecrateClients;

extern std::atomic<uint32_t> g_desecrateHidden;

constexpr int32_t CorpseNoDraw = 104;  // states.txt corpse_nodraw, the same id in PD2 and D2R

// PD2 skips Revive's event 32 and item event functions 14 (item_freeze) and 20 (skill on hit) while a splash hit fires
// its events (the handler set 0x105dc628, read by 0x10347510), so a splash never splashes again or procs on the splashed
// units. Its skill filter (0x105dc5c8, proc_SplashDamage) on events 21 and 30 only refuses a splash inside a splash,
// which Splash does.
extern thread_local bool t_splashing;

constexpr int32_t ProcSplashDamage = 430, GolemSplash = 461, SkeletonSplash = 465;  // merged skill ids

constexpr int32_t IncSplashRadius = 531;
// PD2's damage% calc for the current skill (0x10305eb0): calc1 for these, calc2 for SplashCalc2; others none.
// Merged ids: Joust 450, Blade Dance 452 (PD2 378, 380).

using AreaFn = void (*)(Game*, Unit*, d2r::AreaCenter*, int32_t, uint32_t, int32_t (*)(d2r::AreaContext*, Unit*), void*,
                        int32_t) noexcept;  // rva::AreaUnits

// PD2's callback block (built by srvdo 163): the current skill's fields, or Attack's at level 1

// PD2's callback block (built by srvdo 163): the current skill's fields, or Attack's at level 1
struct SplashHit {
	Unit* struck;
	uint8_t srcDam;
	int32_t skillId, level, percent;
	uint32_t hitFlags;
	uint16_t resultFlags;
	int32_t hitClass;
	int64_t victims;
	// The same callback's other block fields, as Blade Sentinel's think (0x1035fd10) sets them; the splash keeps these
	// defaults. attacker: the unit that hits (null: the area's unit); roll: a to-hit roll with toHit instead of a sure
	// hit; the life leech divisor; the damage percent each damage type is scaled to.
	Unit* attacker = nullptr;
	bool roll = false;
	int32_t toHit = 0;
	uint32_t leechDivisor = 2;
	int32_t damagePercent = 100;
	int64_t inRange = 0;  // the units the callback ran on (victims: those it landed on)
};

// D2R's to-hit roll (attacker, defender, bonus %, 0) -> non-zero on a hit: 1.13c's and PD2's 0x10305140, with no reach
// test. MeleeRoll calls it after its melee reach test (UnitInReach), which an owner far from its sentinel fails.

using ToHitRollFn = int32_t (*)(Unit*, Unit*, int32_t, int32_t) noexcept;

extern ToHitRollFn g_toHitRoll;

constexpr int MeleeRollToHitCall = 0x120;  // MeleeRoll's call to it (93847: 0x44b840)

// D2R's area apply (0x436e90) with a melee hit's events: PD2's apply (0x10305880, its event step 0x10305c30) fires
// domeleedamage on the attacker and event 1 on the victim, where D2R's area apply fires 6 and 2.

// One unit in range (PD2's callback 0x10303190): a hit with no to-hit roll and the current skill's flags, damage% and
// hit class, the skill's elemental and physical damage, weapon damage at SrcDam, life leech halved (at least 1). Counts
// the units the hit lands on. Blade Sentinel's block (SentinelHit) sets the callback's other fields: another attacker,
// a to-hit roll, full leech and a damage percent.
// shortcut: PD2 also writes the skill id to a player victim's data (+0x194 of 1.13c's), not decoded; upgrade with
// the deviation row.
auto SplashOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t;

// The splash around `struck`. Returns 1 as PD2 does, also when nothing is in range or the struck unit has no
// position left (killed by the hit); 0 without a struck unit, or inside another splash.
auto Splash(Game* game, Unit* unit, Unit* struck, int32_t skillId, int32_t level) noexcept -> int64_t;

// The monster data's AI block (unit data +0x30) and its owner id / type, as 0x4a5800 writes and 0x4a53c0 reads them
constexpr int MonDataAi = 0x30, AiOwnerId = 0x68, AiOwnerType = 0x6c;

extern std::atomic<uint32_t> g_sentinelHits;

extern std::atomic<uint32_t> g_sentinelUnits;

using PetOwnerFn = Unit* (*)(Unit* pet) noexcept;
extern PetOwnerFn g_petOwner;

// Slot `rva` must hold `expected` (D2R's function, found by its signature): writes `function` there
auto SwapSlot(const D2RL::PluginContext* context, uint64_t rva, uint64_t expected, const void* function) noexcept -> bool;

// --- Holy Light (PD2 srvdofunc 176) ---------------------------------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 176 at 0x103b3cc0, its per-unit callback 0x103043b0
// (docs/research/skills/w17-holy-light.md). Every unit within aurarangecalc of the target, the caster aside: an ally heals
// cltcalc2-cltcalc3 life, an enemy takes the skill's magic damage; whichever side the target is not on gets half.
extern std::atomic<uint32_t> g_holyLights;

void PutOverlay(Unit* unit, int32_t overlay) noexcept;

// --- Vengeance (PD2 srvdofunc 174) and Joust (PD2 srvdofunc 162 -> router 190) ------------------------------------
// Read from ProjectDiablo.dll: srvdo 174 at 0x103b3570 with its per-unit callback 0x103041f0 and chain hit 0x10303cd0,
// srvdo 162 at 0x103b21e0 (docs/research/skills/w18-vengeance-joust.md).
extern std::atomic<uint32_t> g_vengeances;

extern std::atomic<uint32_t> g_vengeanceLinks;

extern std::atomic<uint32_t> g_jousts;

// Merged states: the aura lists whose elemental damage Vengeance leaves out (holyfire, holywind, holyshock, enchant,
// coldenchant), its chain marker (justhit) and the state each burst clears (shatter)
constexpr int32_t HolyFire = 35, HolyWind = 43, HolyShock = 46, Enchant = 16, ColdEnchant = 223;

// --- Dragon Flight (PD2 srvdofunc 178; docs/research/skills/w31-dragon-flight-psychic-hammer.md) -------------------------
// Read from ProjectDiablo.dll: srvdo 178 at 0x103b4180.
extern std::atomic<uint32_t> g_dragonFlights;

extern std::atomic<uint32_t> g_dragonFlightHits;

constexpr int32_t Bash = 126, UberTalicBash = 580;

// --- Poison Dagger (PD2 srvdofunc 164 -> router 190, cltdofunc 102 -> 122; docs/research/skills/necromancer-w11.md) ---------
// PD2's srvstfunc 68 only calls slot 32, D2R's melee start, so the merge points it there (pd2_port.FUNCS).

// --- Poison Dagger (PD2 srvdofunc 164 -> router 190, cltdofunc 102 -> 122; docs/research/skills/necromancer-w11.md) ---------
// PD2's srvstfunc 68 only calls slot 32, D2R's melee start, so the merge points it there (pd2_port.FUNCS).
extern std::atomic<uint32_t> g_poisonDaggers;

extern std::atomic<uint32_t> g_poisonDaggerClients;

// The skill's rolled poison as D2R's fill makes it (poison +0x38 = min + rand(max - min), length ELen).
auto SkillPoison(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t;

// PD2 adds a skill's poison to a missile's own (Poison Dagger's clouds, Cobra Strike's charge missiles).
void AddMissilePoison(Unit* missile, int32_t poison) noexcept;

// --- Gust, Hunger (PD2 srvdofunc 156 and 165 -> router 190; docs/research/skills/druid-w23.md) --------------------------
extern std::atomic<uint32_t> g_gusts;

extern std::atomic<uint32_t> g_hungers;

// --- Assassin charge-up finishers (PD2 srvdofunc 170 -> router 190, srvprg 171 -> 184, 172 -> 185, 39 -> 186,
// 139 -> 181; docs/research/skills/w29-assassin-finishers.md) ---------------------------------------------------------------
// Tiger Strike, Fists of Fire, Cobra Strike, Claws of Thunder, Blades of Ice, Royal Strike, RakanothStrike (merged ids)
constexpr int32_t TigerStrike = 254, FistsOfFire = 259, CobraStrike = 265, ClawsOfThunder = 269, BladesOfIce = 274,
                  RoyalStrike = 280, RakanothStrike = 630;

extern std::atomic<uint32_t> g_chargeHits;

// PD2's charge spread (0x1037d780): Multiple Shot's line through the target, stepping the other way round, with
// PD2's own handling of a target within 2 subtiles.
struct Spread {
	int32_t x, y, dx, dy;  // first aim point and the step to the next
};


// shortcut: PD2 seeds each maker from a counter it keeps on the caster (+0xcc, bumped per maker); D2R's callback 1
// seeds from its argument plus a path value, so each maker gets the next value of one game-wide counter. Upgrade if
// the walls' paths look less spread than PD2's in play.
// shortcut: makers are capped at 64 (PD2 has no cap; no PD2 row gets near it), upgrade if a row ever needs more.
extern std::atomic<int32_t> g_makerSeeds;

template <typename Create>
auto Walls(Unit* unit, int32_t missile, int32_t count, int32_t skillId, int32_t level,
           int32_t tx, int32_t ty, Create create) noexcept -> int32_t {
	int32_t ux = 0, uy = 0;
	if (!d2r::UnitXY(unit, &ux, &uy)) {
		return 0;
	}
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x21;  // D2R's srvdo 102 / 117 and PD2's 166: start at x/y
	params.owner = unit;
	params.missile = missile;
	params.x = ux;
	params.y = uy;
	params.targetX = tx;
	params.targetY = ty;
	params.skill = skillId;
	params.skillLevel = level;
	params.callback = d2r::rva::MissileCallbackFirestorm;
	int32_t created = 0;
	for (int32_t i = 0; i < std::min(count, 64); ++i) {
		params.callbackArg = g_makerSeeds.fetch_add(1);
		created += create(&params) ? 1 : 0;
	}
	return created;
}


// --- Fire Claws (PD2 srvdofunc 175, cltdofunc 110; docs/research/skills/druid-w24.md) ------------------------------------
// Read from ProjectDiablo.dll: srvdo 175 at 0x103b3ad0, cltdo 110 at 0x103abbd0. A claw hit on the skill's target
// unit, then calc2 makers of srvmissilea (the client: cltmissilea, aimed at the target unit) with Firestorm's maker
// callback. The hit is D2R's Fire Claws hit: its start (srvst 58, kept as data) rolls and queues it with the skill's
// fire damage and srvdo 2 applies it; PD2 puts the fire on its start's buff (srvst 79) and rolls the hit here.
extern std::atomic<uint32_t> g_fireClaws;

extern std::atomic<uint32_t> g_fireClawHits;

extern std::atomic<uint32_t> g_fireClawClients;

// --- Spirit Wolf and Fenris start (PD2 srvstfunc 77; docs/research/skills/druid-w24.md) ----------------------------------
// Read from ProjectDiablo.dll: srvst 77 at 0x103b6c80, its rule 0x103747b0. A player only; pet types 11 and 12 only.
// Once the type has a stored max: while the player has wolves of the type and their count is not under petmax, the
// first one dies; with none left, petmax 0 refuses. D2R's do (srvdo 119) summons at least one even at petmax 0.
extern std::atomic<uint32_t> g_wolfStarts;

extern std::atomic<uint32_t> g_wolfRefusals;

// --- Combustion and the boss novas (PD2 srvdofunc 159 -> router 190, cltdofunc 100 -> 120) ----------------------------
// Read from ProjectDiablo.dll: srvdo 159 at 0x103b1d00, cltdo 100 at 0x103ab580
// (docs/research/skills/w27-static-field-combustion.md). A ring of calc2 missiles of the skill's missile a around the caster.
extern std::atomic<uint32_t> g_novas;

extern std::atomic<uint32_t> g_novaClients;

auto SrvDoNova(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

// Client half (PD2 cltdo 100): the same ring of calc2 cltmissilea.
auto CltDoNova(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t;

// --- Item casts of the novas (PD2 ItemEffect 183 -> 187; IR5, #240) ----------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 183 at 0x103b5080 (its installer 0x1036ba20). An item cast of a nova (a hit skill,
// a charge) makes PD2's ring (0x1036d250: 64 of srvmissilea, flags 3, no range) at the skill target unit's position,
// else the caster's. D2R's do dispatcher (0x43acb0) calls ItemEffect's slot for an item cast, instead of srvdofunc.
// approximated: a unit at the centre takes 1 hit per cast in both: the first missile hits and NextHit blocks the rest for
// NextDelay (4) frames, by which time they have passed it. PD2 also sets the ring missiles' flag 0x80000 (+0xc8), which
// PD2's collision (0x103061d0 -> 0x103a9200, set by 0x103a92e0) uses to widen the block's key (owner, missile row) to any
// player. D2R's collision (0x4639a0 -> 0x324a50, set by 0x324820) keys it by the missile's cast id (+0x12c). So two item
// novas of different casts reaching one unit within 4 frames both hit it here; PD2 lets only the first hit.
// Why not D2R's own nova item effect (srvdo 36, 0x56df80), which also rings 64 missiles at the target: it returns 0 without a
// target unit (PD2 falls back to the caster) and gives the missiles a calc range (PD2's ring has none).
extern std::atomic<uint32_t> g_itemNovas;

// PD2 0x10380670: one step of a cast sequence, the low 20 bits a hash, the top 12 the step count
constexpr auto CastStep(uint32_t v) noexcept -> uint32_t {
	const uint32_t n = v >> 20;
	const uint32_t count = (n < 0xfff ? n + 1 : 1) << 20;
	return (((v & 0xfffff) + count) * 0x6ac690c6u & 0xfffff) + count;
}

// PD2 0x103805f0: CastStep, except that the first cast seeds the sequence from the unit id

// --- Static Field (PD2 srvdofunc 160 -> router 190) ------------------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 160 at 0x103b1dc0, per-unit damage 0x10302830, per-unit debuff 0x1036c920
// (docs/research/skills/w27-static-field-combustion.md). Two passes over the units within aurarangecalc that aurafilter
// takes: lightning damage from current life (StaticFieldDamage), then auratargetstate with aurastat1-6 for
// auralencalc frames.
extern std::atomic<uint32_t> g_staticFields;

// D2Common #11027: current life as a percentage of max life, in whole points
constexpr auto LifePercent(int32_t life, int32_t maxLife) noexcept -> int32_t {
	return (maxLife >> 8) == 0 ? 0 : (life >> 8) * 100 / (maxLife >> 8);
}

// PD2's damage on one unit, in life's 1/256 (0x10302830): calc1 percent of current life, at least calc2; if that,
// lowered by lightresist, would beat (100 - StaticFieldMin) percent of max life (itself lowered by the resist, never
// raised), that max-life share instead. The resist is applied again when the hit lands.

// PD2's 0x1036df10: a resist reduction (damage, magic, fire, light, cold, poison resist) on a monster that is not a
// mercenary and already resists 100 or more is halved.
constexpr auto ImmuneHalved(int32_t value, int32_t stat, bool monsterNotHireling, int32_t current) noexcept -> int32_t {
	const bool resist = stat == 36 || stat == 37 || stat == 39 || stat == 41 || stat == 43 || stat == 45;
	return resist && value < 1 && monsterNotHireling && current > 99 ? value / 2 : value;
}

constexpr size_t AuraStats = 6;  // aurastat1-6


constexpr int32_t TempSkillBuff = 270;

constexpr int32_t SkillFrenzyStat = 169;

constexpr int32_t SplashFrames = 25 * 60 * 10;


extern std::atomic<uint32_t> g_frenzyHits;

constexpr int32_t StatHitpoints = 6, OverlayLifeHeal = 151;

// --- Leap, Leap Attack, Leap Attack Korlic (PD2 srvdofunc 153 -> 172, cltdofunc 97 -> 115) ------------------------------
// Read from ProjectDiablo.dll: srvdo 153 at 0x103b1300, cltdo 97 at 0x103ab280 (docs/features/core-rules.md). PD2's leap path is
// D2R's (path type 9 to the skill's point); PD2 sets its own speed and, on landing, hits every unit within calc3.
extern std::atomic<uint32_t> g_leaps;

extern std::atomic<uint32_t> g_leapClients;

using UnitSkill = uint8_t;


auto UnitSkillOf(Unit* unit, int32_t skillId) noexcept -> UnitSkill*;

auto Flags(UnitSkill* uskill) noexcept -> uint32_t&;

void SetSkillVelocity(Unit* unit, UnitSkill* uskill, int32_t velocity) noexcept;

// 93847: 0x22e8f0, void (unit, unitSkill): the client leap's jump arc (gravity and lift), timed from the path's
// velocity at the call. D2R's cltdo 43 runs it on the start frame with LeapStart's speed.
extern sigscan::Entry LeapArc;

// --- Inferno, Arctic Blast (PD2 srvdofunc 182, cltdofunc 111) ----------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 182 at 0x103b4e40, cltdo 111 at 0x103abce0 (docs/research/skills/druid-w21.md). A player
// sends calc3 flames of range calc1 in a line across the aim; a monster keeps D2R's Inferno (srvdo 19).
extern std::atomic<uint32_t> g_channels;

extern std::atomic<uint32_t> g_channelClients;

// --- The channel take-over (PD2's gate A, ProjectDiablo.dll 0x10373290; docs/research/skills/druid-w21.md) ---------------
// PD2 keys two rules on the busy player's current skill: Inferno / Arctic Blast (srvdofunc 182) give way at once to
// any request that carries a skill; Blade Fury (48) gives way to any other skill and refuses another Blade Fury, so its
// channel goes on. Everything else is D2R's own gate.
extern std::atomic<uint32_t> g_takeOvers;

extern std::atomic<uint32_t> g_bladeFuryRefusals;

extern std::atomic<uint32_t> g_heldRequests;

constexpr int32_t Uninterruptable = 54;  // states.txt uninterruptable: PD2 refuses before its own rules

auto UnitSkillIdOf(void* unitSkill) noexcept -> int32_t;

// --- Charge (PD2 srvstfunc 72, srvdofunc 180; cltstfunc 57 -> 25, cltdofunc 112 -> 37) -------------------------------
// Read from ProjectDiablo.dll: srvst 72 at 0x103b67e0, srvdo 180 at 0x103b4870, cltst 57 at 0x103ace50, cltdo 112 at
// 0x103abfb0 (docs/research/skills/w16-charge.md). PD2's charge is vanilla's with two changes on the server: the start's speed
// adds Param1, and the hit puts calc'd splash on the caster from how far it charged. D2R's client halves (cltst 25,
// cltdo 37) already add Param1, so the merge points the client columns there.
extern std::atomic<uint32_t> g_charges;

extern std::atomic<uint32_t> g_chargeHitFrames;

auto CurrentUnitSkill(Unit* unit) noexcept -> UnitSkill*;

// --- Revive's aura event (PD2 auraeventfunc 32, ProjectDiablo.dll 0x10347370) ---------------------------------------
// Revive's row has auraevent1 domeleedamage: each melee hit of a revived monster runs proc_SplashDamage's do function
// (PD2 358, merged 430) at level 1 from it, called directly as PD2 does. PD2's splash finds the struck unit on the
// revive's path; this passes the unit the hit landed on.
extern std::atomic<uint32_t> g_reviveEvents;

extern std::atomic<uint32_t> g_reviveSplashes;

// Revive's do (srvdofunc 58): D2R's own, then Revive's aura events go on the revived monster. D2R's Revive never
// registers them, so without this event 32 never fires. The corpse is revived in place (seen in game: same unit,
// result 1, mode neutral), so the target before the call is the pet after it; a refused revive leaves it dead.
extern std::atomic<uint32_t> g_revives;

constexpr int32_t StateRevive = 96;

auto SrvDoRevive(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

// --- Frozen Armor and Enchant on allies (PD2 srvdofunc 25, #1870) ----------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 25 at 0x103aec40 (installed 0x103aecb0), its per-unit callback 0x1036c920.
// D2R's srvdo 25 (0x553650) puts aurastate on one unit (the skill's target, else the caster). PD2's builds
// {skill, level, auralencalc, auratargetstate, aurastat1-6, their aurastatcalc values, divisor 2}, runs the callback
// on the caster, then on every other unit within aurarangecalc that aurafilter takes (D2Game's area search 0xa0c70,
// D2R's AreaUnits, which skips the caster; flag 1). The callback halves each value (a tenth for enchant/coldenchant on
// a revive) for another player, a hireling or a unit whose flags +0xc4 (D2R +0x124) have the top bit; the caster and
// its other pets get the full value. The row's own tooltip says allies get half.
extern std::atomic<uint32_t> g_allyBuffs;

// --- Holy Sword and Smite starts (PD2 srvstfunc 73, 75), Smite's crushing blow (srvdo 150) -------------------------
// Read from ProjectDiablo.dll: srvst 73 at 0x103b6990, srvst 75 at 0x103b6a20, event 16 at 0x103469a0
// (docs/research/skills/w19-holy-sword-smite.md).
extern std::atomic<uint32_t> g_holySwordStarts;

extern std::atomic<uint32_t> g_smiteStarts;

extern std::atomic<uint32_t> g_smiteEvents;

constexpr int32_t SkillSmite = 97;                              // merged id, PD2's

constexpr int32_t StatCrushingBlow = 136;                       // itemstatcost crushingblow

// Holy Sword start: the weapon must be both a sword and two-handed.

using AuraEventFn = int32_t (*)(Game*, int32_t, Unit*, Unit*, void*, int32_t, int32_t) noexcept;

// PD2's event 16 on Smite: one roll a hit with the crushingblow stat, its per-hand sum and calc3, on the damage block
// before D2R applies it, as a domeleedamage event runs. CrushingBlowApply's slot holds PD2's blow (item_rules.cpp,
// with Smite's calc4).
// shortcut: if D2R also rolls item crushing blow inline (outside the dump), a Smite hit rolls the item's chance twice;
// upgrade if the game check shows more blows than crushingblow + calc3 allows.

using QueueHitFn = void (*)(Game*, Unit*, Unit*, uint8_t*, uint8_t) noexcept;
extern QueueHitFn g_queueHitOriginal;

// D2R's hit queue (inline hook): Smite's hit gets its crushing-blow roll when it landed (D2R's own test there).
void QueueHitHook(Game* game, Unit* unit, Unit* target, uint8_t* damage, uint8_t flag) noexcept;

// srvdo 150 (D2R's Smite do, also MonSmite's, UberTalicSmite's, MonSmiteLucion's): only the Smite row rolls, as only
// its PD2 start adds the event.
auto SrvDoSmite(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

// --- Monster kits (W36; docs/research/skills/w36-monster-kits.md) ---------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 173 at 0x103b3520, cltdo 109 at 0x103abba0, srvdo 181 at 0x103b4cf0, srvst 67 at
// 0x103b6520 with its per-unit callback 0x1036d5a0.
extern std::atomic<uint32_t> g_eggSpawns;

extern std::atomic<uint32_t> g_eggSpawnClients;

extern std::atomic<uint32_t> g_nestSpawns;

extern std::atomic<uint32_t> g_nestFull;

extern std::atomic<uint32_t> g_cyclerStarts;

}  // namespace pd2r::skills
