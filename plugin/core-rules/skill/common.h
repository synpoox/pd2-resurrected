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

using MoverFn = uint64_t (*)(d2r::Game*, d2r::Unit* owner, d2r::Unit* pet, int32_t kind, int32_t, int32_t, uint8_t radius) noexcept;
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
// `lastFighting` get the distance, the kind picked and whether the 3.2 limit applied.
inline auto Leash(const Engine& engine, d2r::Game* game, d2r::Unit* pet, d2r::Unit* target, d2r::Unit* owner, int32_t radius,
                  std::atomic<uint32_t>* moves, int64_t* lastD2 = nullptr, int32_t* lastMove = nullptr,
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
	// moves; D2R's mover kinds and the owner's position stand in. Upgrade if pets lag a running owner.
	return engine.mover(game, owner, pet, move, 0, 0, static_cast<uint8_t>(move == MoveWarp ? 0 : radius));
}

}  // namespace leash

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
