// Melee reach (#1977): PD2 widens 1.13c's in-reach test by who owns the attacker. Decoded from the installed
// ProjectDiablo.dll (sha256 e7eec256...; PD2.gpr addresses + 0x10 below 0x1035xxxx, + 0x70 above):
// - D2Common #11138 (unit, target, range) at 41 of D2Game's 42 call sites -> 0x103049f0 (PD2.gpr 0x103049e0): the
//   owner is 0x10374a60(unit, 1) (PD2.gpr 0x103749f0: a player itself, a monster its AI owner, else itself). A monster
//   owner adds 1 to range; a player owner adds #11133(unit) / 2 (the unit's own melee range, truncated) plus
//   #10143(target) (1 while the target walks or runs). Then 1.13c's #11138 with that range.
// - D2Game +0xde5a0, the melee roll, at 31 of its 34 call sites -> 0x103048b0 (PD2.gpr 0x103048a0, through the thunk
//   0x103a3960): its reach is arg + 2 plus the same growth, handed to 0x103049f0, which adds the growth again. 1.13c's
//   reach there is 3 for a monster, 1 for anyone else.
// - The area search +0xa0f40 at +0x28d21 (srvdo 76, Whirlwind's next target, radius 5) -> 0x10374ba0 (thunk
//   0x103a52b0): radius #11133(unit) + 2, plus 1 for a monster owner or #11133(unit) / 2 for a player owner, capped at 6
//   when the owner is on PD2's PvP levels 157, 159, 166. Its filter 0x10374b30 (in place of 1.13c's +0x9ed70, which takes
//   the next unit id above the last target, wrapping) takes the NEAREST unit by dx*dx + dy*dy (0x1037d8b0; best seeded
//   0x7fffffff), skips the last target's id and falls back to it only when nothing else is in range.
// D2R: #11138's twin is UnitInReach (0x348650; #11133's twin 0x34a620), +0xde5a0's is MeleeRoll (0x44b720, reach
// UnitReach + its last arg into UnitInReach at 0x44b803), srvdo 76 searches with NearestTarget (radius 5 at 0x56924a).
// D2RCore and Cartographer neither call nor hook UnitInReach, so its entry is hooked: every caller, server and client,
// takes the growth (so a joined client picks attack or walk as the server does), and the MeleeRoll call takes PD2's
// roll reach. The unredirected 1.13c sites (+0xa7fcb monster AI; rolls +0x4f872 srvst 16, +0x77788, +0xa1185, which got one
// growth through the roll's redirected #11138) and D2Common's own caller follow ours (docs/deviations.md).
#include "common.h"

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

constexpr int UnitIdField = 0x08;  // uint32 unit id (0x34a330 reads it)
constexpr int32_t PlayerWalk = 2, PlayerRun = 3, PlayerTownWalk = 6;
constexpr int32_t MonsterWalk = 2, MonsterRun = 15, MonsterSkill1 = 8, MonsterSkill2 = 9;
// #10143 counts S1/S2 as moving for monstats BaseId vulture1 (row 110): vulture1-4, vulture5, vultureSiege
constexpr auto VultureBase(int32_t classId) noexcept -> bool {
	return (classId >= 110 && classId <= 113) || classId == 609 || classId == 861;
}
static_assert(VultureBase(110) && VultureBase(113) && VultureBase(609) && VultureBase(861) && !VultureBase(114) &&
              !VultureBase(109));
// The PvP cap stays cut: level ids 157, 159 and 166 are Cartographer areas here, not PvP maps.
// NearestTarget: mov rcx,rdi / movdqa [rsp+0x50],xmm0 / mov [rsp+0x20],eax / call AreaIterate (0x4398b0) / jmp
constexpr const char* AreaIterateCall = "48 8b cf 66 0f 7f 44 24 50 89 44 24 20 e8 ?? ?? ?? ?? eb 12";
constexpr int AreaIterateCallAt = 13;

using ReachFn = int32_t (*)(Unit*, Unit*, int32_t, int32_t*) noexcept;
using RangeFn = int32_t (*)(Unit*) noexcept;
using NearestFn = Unit* (*)(d2r::Game*, Unit*, int32_t, int32_t, int32_t, uint32_t, uint32_t, int32_t*) noexcept;
ReachFn g_reach = nullptr;
RangeFn g_meleeRange = nullptr;
uint64_t g_nearest = 0, g_rollReturn = 0;

// D2R's area iterator (game, unit, &{room, x, y}, radius, flags | 0xa783, filter, ctx, 0, file, line); the filter
// gets (iterator state, candidate) and finds ctx at state + 0x18
struct AreaPos {
	void* room;
	int32_t x, y;
};
using FilterFn = int32_t (*)(uint8_t*, Unit*) noexcept;
using AreaFn = void (*)(d2r::Game*, Unit*, AreaPos*, int32_t, uint32_t, FilterFn, void*, int32_t, const char*,
                        int32_t) noexcept;
AreaFn g_area = nullptr;
struct Nearest {
	Unit* self;
	uint32_t skip;
	Unit* best;
	int64_t bestDist;
	Unit* last;
	int32_t candidates;
	uint32_t nextId, lowId;  // for the hot log only: D2R's pick (the next id above the last target, else the lowest)
};

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
		       ((mode == MonsterSkill1 || mode == MonsterSkill2) && VultureBase(At<int32_t>(target, d2r::rva::UnitClassId)));
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
	return in;
}

// PD2's filter 0x10374b30: the nearest other unit by squared distance; the last target only when nothing else is
auto NearestFilter(uint8_t* state, Unit* unit) noexcept -> int32_t {
	auto* ctx = At<Nearest*>(state, 0x18);
	int32_t ux = 0, uy = 0, sx = 0, sy = 0;
	if (unit == nullptr || !d2r::UnitXY(unit, &ux, &uy) || !d2r::UnitXY(ctx->self, &sx, &sy)) {
		return 1;
	}
	++ctx->candidates;
	const uint32_t id = At<uint32_t>(unit, UnitIdField);
	if (id > ctx->skip && id < ctx->nextId) {
		ctx->nextId = id;
	}
	if (id < ctx->lowId) {
		ctx->lowId = id;
	}
	if (id == ctx->skip) {
		ctx->last = unit;
		return 1;
	}
	const int64_t dx = ux - sx, dy = uy - sy, dist = dx * dx + dy * dy;
	if (dist < ctx->bestDist) {
		ctx->best = unit;
		ctx->bestDist = dist;
	}
	return 1;
}

// srvdo 76's next target: 0x10374ba0's radius and nearest pick instead of 5 and the next unit id
auto WhirlSearch(d2r::Game* game, Unit* unit, int32_t x, int32_t y, int32_t radius, uint32_t flags, uint32_t skip,
                 int32_t* out) noexcept -> Unit* {
	if (game == nullptr || unit == nullptr) {
		return reinterpret_cast<NearestFn>(g_nearest)(game, unit, x, y, radius, flags, skip, out);
	}
	const Owner owner = OwnerOf(unit);
	const int32_t range = g_meleeRange(unit);
	const int32_t widened = range + 2 + (owner == Owner::Monster ? 1 : owner == Owner::Player ? range / 2 : 0);
	Nearest ctx {unit, skip, nullptr, 0x7fffffff, nullptr, 0, UINT32_MAX, UINT32_MAX};
	AreaPos pos {d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::UnitRoom)(unit), x, y};
	g_area(game, unit, &pos, widened, flags | 0xa783, &NearestFilter, &ctx, 0, __FILE__, __LINE__);
	Unit* pick = ctx.best != nullptr ? ctx.best : ctx.last;
	if (out != nullptr) {
		*out = ctx.candidates;
	}
	return pick;
}

// Why a part is off, for the startup log: the first check that failed (nullptr = on)
auto ScanOff(int matches) noexcept -> const char* {
	return matches == 0 ? "the signature matches nowhere" : "the signature matches more than once (or is bad)";
}

}  // namespace

auto InstallMeleeReach(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	using sigscan::State;
	uint64_t rva = 0;
	const int rangeN = sigscan::ScanUnique(context->exeBase, MeleeRangePattern, 0, &rva);
	if (rangeN == 1) {
		g_meleeRange = reinterpret_cast<RangeFn>(context->exeBase + rva);
	}
	const int rollN = sigscan::ScanUnique(context->exeBase, RollReachCall, 0, &rva);
	if (rollN == 1) {
		g_rollReturn = context->exeBase + rva + RollReachCallAt + 5;
	}
	const char* testOff = nullptr;
	const char* testPart = nullptr;
	if (rangeN != 1) {
		testPart = "MeleeRangePattern"; testOff = ScanOff(rangeN);
	} else if (rollN != 1) {
		testPart = "RollReachCall"; testOff = ScanOff(rollN);
	} else if (d2r::sig::UnitReach.state != State::Ok) {
		testPart = "UnitReach"; testOff = "the shared signature did not resolve";
	} else if (d2r::sig::UnitInReach.state != State::Ok) {
		testPart = "UnitInReach"; testOff = "the shared signature did not resolve";
	} else if (!sigscan::HookBySignature(context, "UnitInReach", d2r::sig::UnitInReach.pattern,
	                                     d2r::sig::UnitInReach.at,  // the shared pattern starts past the entry
	                                     ReachEntry, sizeof(ReachEntry), &Reach, &g_reach)) {
		testPart = "UnitInReach hook"; testOff = "the entry hook did not install (another plugin's patch?)";
	}
	const bool test = testOff == nullptr;
	const int areaN = sigscan::ScanUnique(context->exeBase, AreaIterateCall, 0, &rva);
	if (areaN == 1) {
		const uint64_t site = context->exeBase + rva + AreaIterateCallAt;
		int32_t rel = 0;
		std::memcpy(&rel, reinterpret_cast<const void*>(site + 1), 4);
		g_area = reinterpret_cast<AreaFn>(site + 5 + static_cast<int64_t>(rel));
	}
	const char* searchOff = nullptr;
	const char* searchPart = nullptr;
	if (rangeN != 1) {
		searchPart = "MeleeRangePattern"; searchOff = ScanOff(rangeN);
	} else if (areaN != 1) {
		searchPart = "AreaIterateCall"; searchOff = ScanOff(areaN);
	} else if (d2r::sig::UnitRoom.state != State::Ok) {
		searchPart = "UnitRoom"; searchOff = "the shared signature did not resolve";
	} else if (!RedirectCall(context, WhirlSearchCall, WhirlSearchCallAt, reinterpret_cast<uint64_t>(&WhirlSearch),
	                         &g_nearest)) {
		searchPart = "WhirlSearchCall"; searchOff = "the call redirect did not install";
	}
	const bool search = searchOff == nullptr;
	char testText[160] = "on", searchText[160] = "on";
	if (!test) {
		std::snprintf(testText, sizeof(testText), "off (%s: %s)", testPart, testOff);
	}
	if (!search) {
		std::snprintf(searchText, sizeof(searchText), "off (%s: %s)", searchPart, searchOff);
	}
	plog::Info("melee-reach: pd2 reach test and roll %s, whirlwind search %s", testText, searchText);
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; melee-reach%s", test && search ? "" : " FAILED");
	}
	return test && search;
}

#include "registry.h"
PD2R_RULE(InstallMeleeReach, 160)
