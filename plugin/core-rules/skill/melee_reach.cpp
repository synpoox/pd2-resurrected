// Melee reach (#1977): PD2 widens 1.13c's in-reach test by who owns the attacker. Decoded from the installed
// ProjectDiablo.dll (sha256 e7eec256...; PD2.gpr addresses + 0x10 below 0x1035xxxx, + 0x70 above):
// - D2Common #11138 (unit, target, range) at 41 of D2Game's 42 call sites -> 0x103049f0 (PD2.gpr 0x103049e0): the
//   owner is 0x10374a60(unit, 1) (PD2.gpr 0x103749f0: a player itself, a monster its AI owner, else itself). A monster
//   owner adds 1 to range; a player owner adds #11133(unit) / 2 (the unit's own melee range, truncated) plus
//   #10143(target) (1 while the target walks or runs). Then 1.13c's #11138 with that range.
// - D2Game +0xde5a0, the melee roll, at 31 of its 34 call sites -> 0x103048b0 (PD2.gpr 0x103048a0, through the thunk
//   0x103a3960): its reach is arg + 2 plus the same growth, handed to 0x103049f0, which adds the growth again. 1.13c's
//   reach there is 3 for a monster, 1 for anyone else.
// - The area search +0xa0f40 at +0x28d21 (srvdo 76, Whirlwind's next target, radius 5) -> 0x10374ba0 (PD2.gpr
//   0x10374b30, thunk 0x103a52b0): radius #11133(unit) + 2, plus 1 for a monster owner or #11133(unit) / 2 for a player
//   owner, capped at 6 when the owner is on PD2's PvP levels 157, 159, 166 (cut: the port has no PvP).
// D2R: #11138's twin is UnitInReach (0x348650; #11133's twin 0x34a620), +0xde5a0's is MeleeRoll (0x44b720, reach
// UnitReach + its last arg into UnitInReach at 0x44b803), srvdo 76 searches with NearestTarget (radius 5 at 0x56924a).
// D2RCore and Cartographer neither call nor hook UnitInReach, so its entry is hooked: every caller, server and client,
// takes the growth (so a joined client picks attack or walk as the server does), and the MeleeRoll call takes PD2's
// roll reach. The 4 unredirected 1.13c sites (functions with no direct caller) and D2Common's own caller are widened
// too (docs/deviations.md).
#include "common.h"

#include "hot_log.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"

#include "../item_rules.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace {

using d2r::At;
using d2r::Unit;

// mov [rsp+8],rbx / mov [rsp+0x18],rbp / mov [rsp+0x20],rsi: 3 whole instructions
constexpr uint8_t ReachEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x18,
                                  0x48, 0x89, 0x74, 0x24, 0x20};
// 0x34a620 (unit) -> melee range: a player's weapon range, a monster's monstats2 MeleeRng
constexpr const char* MeleeRangePattern = "40 55 56 57 48 83 ec 50 48 8b f9 48 85 c9 75 35 88 4c 24 70";
// MeleeRoll: mov r8d,[rsp+0x88] (last arg) / xor r9d,r9d / add r8d,eax (UnitReach) / mov rdx,rdi / mov rcx,rsi / call
constexpr const char* RollReachCall = "44 8b 84 24 88 00 00 00 45 33 c9 44 03 c0 48 8b d7 48 8b ce e8";
constexpr int RollReachCallAt = 20;
// srvdo 76: flags 3 / mov rcx,r13 / radius 5 / call NearestTarget
constexpr const char* WhirlSearchCall = "c7 44 24 28 03 00 00 00 49 8b cd c7 44 24 20 05 00 00 00 e8";
constexpr int WhirlSearchCallAt = 19;

constexpr int32_t PlayerWalk = 2, PlayerRun = 3, PlayerTownWalk = 6;
constexpr int32_t MonsterWalk = 2, MonsterRun = 15, MonsterSkill1 = 8, MonsterSkill2 = 9;
constexpr int32_t Vulture = 110;  // #10143: vulture1 also moves in S1/S2

using ReachFn = int32_t (*)(Unit*, Unit*, int32_t, int32_t*) noexcept;
using RangeFn = int32_t (*)(Unit*) noexcept;
using NearestFn = Unit* (*)(d2r::Game*, Unit*, int32_t, int32_t, int32_t, uint32_t, uint32_t, int32_t*) noexcept;
ReachFn g_reach = nullptr;
RangeFn g_meleeRange = nullptr;
uint64_t g_nearest = 0, g_rollReturn = 0;
std::atomic<uint32_t> g_tests {0}, g_rolls {0}, g_searches {0};

enum class Owner { None, Monster, Player };

// 0x10374a60 by the unit's owner fields. shortcut: a monster owned by a monster counts as a monster (PD2 asks that
// owner's owner too); missiles get no growth. Upgrade if a player's summon summons.
auto OwnerOf(const Unit* unit) noexcept -> Owner {
	switch (At<int32_t>(unit, d2r::rva::UnitType)) {
	case 0:
		return Owner::Player;
	case 1: {
		const uint32_t id = At<uint32_t>(unit, d2r::rva::UnitOwnerId);
		const bool owned = id != 0 && id != UINT32_MAX;
		return owned && At<int32_t>(unit, d2r::rva::UnitOwnerType) == 0 ? Owner::Player : Owner::Monster;
	}
	default:
		return Owner::None;
	}
}

// #10143: walking or running. shortcut: a player in a skill sequence is read by its unit mode (1.13c reads the
// sequence's mode); no PD2 skill sequence walks.
auto Moving(const Unit* target) noexcept -> bool {
	const int32_t mode = At<int32_t>(target, d2r::rva::UnitMode);
	switch (At<int32_t>(target, d2r::rva::UnitType)) {
	case 0:
		return mode == PlayerWalk || mode == PlayerRun || mode == PlayerTownWalk;
	case 1:
		return mode == MonsterWalk || mode == MonsterRun ||
		       ((mode == MonsterSkill1 || mode == MonsterSkill2) && At<int32_t>(target, d2r::rva::UnitClassId) == Vulture);
	default:
		return false;
	}
}

// 0x103049f0's growth of the range
auto Growth(Unit* unit, Unit* target, Owner owner) noexcept -> int32_t {
	if (owner == Owner::Monster) {
		return 1;
	}
	return owner == Owner::Player ? g_meleeRange(unit) / 2 + (Moving(target) ? 1 : 0) : 0;
}

auto Reach(Unit* unit, Unit* target, int32_t range, int32_t* reach) noexcept -> int32_t {
	if (unit == nullptr || target == nullptr) {
		return g_reach(unit, target, range, reach);
	}
	const Owner owner = OwnerOf(unit);
	const int32_t growth = Growth(unit, target, owner);
	const bool roll = reinterpret_cast<uint64_t>(_ReturnAddress()) == g_rollReturn;
	// PD2's roll: its last arg + 2 + growth instead of UnitReach + last arg, then 0x103049f0's growth on top
	const int32_t widened = roll ? range - d2r::Engine<RangeFn>(d2r::sig::UnitReach)(unit) + 2 + growth * 2 : range + growth;
	const int32_t in = g_reach(unit, target, widened, reach);
	if (pd2::HotLogsOn() && pd2::HotSample(roll ? g_rolls : g_tests)) {
		plog::Info("melee-reach: %s unit type %d class %d (%s owner) -> target type %d class %d mode %d, range %d -> %d, "
		           "in reach %d",
		           roll ? "roll" : "test", At<int32_t>(unit, d2r::rva::UnitType), At<int32_t>(unit, d2r::rva::UnitClassId),
		           owner == Owner::Player ? "player" : owner == Owner::Monster ? "monster" : "no",
		           At<int32_t>(target, d2r::rva::UnitType), At<int32_t>(target, d2r::rva::UnitClassId),
		           At<int32_t>(target, d2r::rva::UnitMode), range, widened, in);
	}
	return in;
}

// srvdo 76's next target: 0x10374ba0's radius instead of 5
auto WhirlSearch(d2r::Game* game, Unit* unit, int32_t x, int32_t y, int32_t radius, uint32_t flags, uint32_t skip,
                 int32_t* out) noexcept -> Unit* {
	if (unit != nullptr) {
		const Owner owner = OwnerOf(unit);
		const int32_t range = g_meleeRange(unit);
		const int32_t widened = range + 2 + (owner == Owner::Monster ? 1 : owner == Owner::Player ? range / 2 : 0);
		if (pd2::HotSample(g_searches)) {
			plog::Info("melee-reach: whirlwind search unit type %d, radius %d -> %d (%u so far)",
			           At<int32_t>(unit, d2r::rva::UnitType), radius, widened, g_searches.load());
		}
		radius = widened;
	}
	return reinterpret_cast<NearestFn>(g_nearest)(game, unit, x, y, radius, flags, skip, out);
}

}  // namespace

auto InstallMeleeReach(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	using sigscan::State;
	uint64_t rva = 0;
	if (sigscan::ScanUnique(context->exeBase, MeleeRangePattern, 0, &rva) == 1) {
		g_meleeRange = reinterpret_cast<RangeFn>(context->exeBase + rva);
	}
	if (sigscan::ScanUnique(context->exeBase, RollReachCall, 0, &rva) == 1) {
		g_rollReturn = context->exeBase + rva + RollReachCallAt + 5;
	}
	const bool test = g_meleeRange != nullptr && g_rollReturn != 0 && d2r::sig::UnitReach.state == State::Ok &&
	                  d2r::sig::UnitInReach.state == State::Ok &&
	                  sigscan::HookBySignature(context, "UnitInReach", d2r::sig::UnitInReach.pattern, 0, ReachEntry,
	                                           sizeof(ReachEntry), &Reach, &g_reach);
	const bool search = g_meleeRange != nullptr &&
	                    RedirectCall(context, WhirlSearchCall, WhirlSearchCallAt, reinterpret_cast<uint64_t>(&WhirlSearch),
	                                 &g_nearest);
	plog::Info("melee-reach: pd2 reach test and roll %s, whirlwind search %s", test ? "on" : "FAILED",
	           search ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; melee-reach%s", test && search ? "" : " FAILED");
	}
	return test && search;
}

#include "registry.h"
PD2R_RULE(InstallMeleeReach, 160)
