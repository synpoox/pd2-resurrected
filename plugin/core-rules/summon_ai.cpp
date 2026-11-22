// PD2's changes to the ShadowMaster summon AI (#1790): monai row 106, used by the Valkyrie, the Decoy (dopplezonnew) and
// the Shadow Master. The tables equal PD2's; the difference is engine code. ProjectDiablo.dll keeps 1.13c's think
// (D2Game +0xab580) and patches it (its patch table, docs/pd2/summon-ai.md):
// - both calls of 1.13c's leash (+0xab66e, +0xab939, callee +0x1e140) go to PD2's own (0x10360d40 in the installed
//   DLL). 1.13c's leash, called whenever the pet is more than aip2(H) = 13 subtiles from its owner, walks it back
//   once it is more than about 6 away, target or not: the Valkyrie ran at a far monster, then turned back. PD2's leash
//   lets the pet fight up to sqrt(3.2 * 361) = 34 subtiles out and idle up to 19 (owner, then pet, squared distances).
// - five branches become `jmp`: the buff branch always casts (+0xab865), the pet never takes its owner's target
//   (+0xab8e2), no "basic attack" shortcut (+0xab99d), no step back or aside in a crowd (+0xabb48, the Decoy's
//   "reposition" run), no fallback attack when no skill scored (+0xac337).
// D2R's think (0x5cd740) is the same code, so the same five branches are patched here, each found by a unique
// signature, through the MutationService (no SDK service changes an AI). The leash (0x5bd1e0) is hooked at its entry:
// no SDK service runs inside an AI think. Only the two calls from the ShadowMaster think (their return addresses) get
// PD2's rule; every other caller (other pets) keeps D2R's leash.
#include "summon_ai.h"

#include "pd2_engine.h"
#include "log.h"
#include "sig.h"

#include <D2RLPlugin/mutations.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {

using d2r::Game;
using d2r::Unit;

// D2R's mover kinds (0x5bc940's fourth argument, as D2R's leash picks them)
constexpr int32_t MoveNone = 0, MoveBack = 1, MoveWarp = 3, MoveWander = 4, MoveAside = 5;

// PD2's leash rule (0x10360d40): d2 = owner-to-pet squared distance; `fighting` = the pet has a target
constexpr auto LeashMove(int64_t d2, bool fighting, bool wanderRoll) noexcept -> int32_t {
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
static_assert(LeashMove(361, false, false) == MoveNone && LeashMove(362, false, false) == MoveBack);
static_assert(LeashMove(1155, true, false) == MoveNone && LeashMove(1156, true, false) == MoveBack);
static_assert(LeashMove(0x900, true, false) == MoveWarp && LeashMove(5, false, false) == MoveAside);
static_assert(LeashMove(100, false, true) == MoveWander && LeashMove(100, true, true) == MoveNone);

using LeashFn = uint64_t (*)(Game*, Unit* pet, Unit* target, Unit* owner, int32_t flag, void* tick, int32_t, int32_t radius) noexcept;
using MoverFn = uint64_t (*)(Game*, Unit* owner, Unit* pet, int32_t kind, int32_t, int32_t, uint8_t radius) noexcept;
using PathTargetFn = Unit* (*)(Game*, Unit*) noexcept;
using IsDeadFn = int32_t (*)(Unit*) noexcept;

LeashFn g_leashOriginal = nullptr;
MoverFn g_mover = nullptr;
PathTargetFn g_pathTarget = nullptr;
IsDeadFn g_isDead = nullptr;
uint64_t g_leashReturn[2] = {};  // absolute return addresses of the think's two leash calls

std::atomic<uint32_t> g_leashCalls {0};
std::atomic<uint32_t> g_moves[6] {};

auto Dist2(Unit* a, Unit* b) noexcept -> int64_t {
	int32_t ax = 0, ay = 0, bx = 0, by = 0;
	if (!d2r::UnitXY(a, &ax, &ay) || !d2r::UnitXY(b, &bx, &by)) {
		return -1;
	}
	return int64_t {ax - bx} * (ax - bx) + int64_t {ay - by} * (ay - by);
}

auto SummonLeash(Game* game, Unit* pet, Unit* target, Unit* owner, int32_t flag, void* tick, int32_t a7,
                 int32_t radius) noexcept -> uint64_t {
	const auto from = reinterpret_cast<uint64_t>(_ReturnAddress());
	if (from != g_leashReturn[0] && from != g_leashReturn[1]) {
		return g_leashOriginal(game, pet, target, owner, flag, tick, a7, radius);
	}
	++g_leashCalls;
	// PD2 leashes only a player's pet
	if (game == nullptr || pet == nullptr || owner == nullptr || d2r::At<int32_t>(owner, d2r::rva::UnitType) != 0) {
		return 0;
	}
	const int64_t d2 = Dist2(owner, pet);
	if (d2 < 0) {
		return 0;
	}
	bool fighting = target != nullptr;
	if (!fighting) {
		// PD2: the unit the pet's path is after, a live player or monster within 34 subtiles
		Unit* chase = g_pathTarget(game, pet);
		fighting = chase != nullptr && chase != owner && d2r::At<int32_t>(chase, d2r::rva::UnitType) <= 1 &&
		           g_isDead(chase) == 0 && Dist2(pet, chase) >= 0 && Dist2(pet, chase) <= 1155;
	}
	uint32_t* seed = d2r::api.unitSeed != nullptr ? d2r::api.unitSeed(pet) : nullptr;
	const bool wander = seed != nullptr && d2r::RandomBelow(seed, 12) == 0;
	const int32_t move = LeashMove(d2, fighting, wander);
	++g_moves[move];
	if (move == MoveNone) {
		return 0;
	}
	// shortcut: PD2 walks back with its own mover (0x10356a40) and measures to the owner's path end while the owner
	// moves; D2R's mover kinds and the owner's position stand in. Upgrade if pets lag a running owner.
	return g_mover(game, owner, pet, move, 0, 0, static_cast<uint8_t>(move == MoveWarp ? 0 : radius));
}

// The leash's entry: mov [rsp+10],rbx / mov [rsp+18],rbp / mov [rsp+20],rsi
constexpr uint8_t LeashEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20};
constexpr const char* LeashPattern = "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 48 89 4C 24 08 57 41 54 41 55 41 56 41 57 "
                                     "48 83 EC 60 4C 8B F9 49 8B F1 49 8B C9 49 8B D8 4C 8B F2 E8";
// In the leash, its one mover call (rcx game, rdx owner, r8 pet, r9d kind, two zeros, the radius byte); E8 at +19
constexpr const char* MoverCallPattern = "40 88 6C 24 30 4D 8B C6 89 7C 24 28 48 8B D6 89 7C 24 20 E8";
// The think's two leash calls, to their return addresses: the aip2(H) gate (+0x41) and the near-owner call (+0x35)
constexpr const char* LeashCall1Pattern =
	"0F BF 8A 9A 00 00 00 0F AF C9 3B C1 7E 3B 48 8B 45 88 45 33 C0 4C 8B 4D 98 48 8B D7 C7 44 24 38 06 00 00 00 49 8B "
	"CD C7 44 24 30 00 00 00 00 48 89 44 24 28 8B 44 24 68 89 44 24 20 E8 ?? ?? ?? ?? 85 C0";
constexpr const char* LeashCall2Pattern =
	"3D 90 00 00 00 7F 3C 8B 44 24 68 4D 8B CF C7 44 24 38 06 00 00 00 4D 8B C5 C7 44 24 30 00 00 00 00 48 8B D7 4C 89 "
	"74 24 28 49 8B CC 89 44 24 20 E8 ?? ?? ?? ?? 85 C0";
// The think's calls of the path-target getter (E8 at +6) and of the dead test (E8 at +3)
constexpr const char* PathTargetCallPattern = "48 8B D7 49 8B CD E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 75 12";
constexpr const char* IsDeadCallPattern = "48 8B C8 E8 ?? ?? ?? ?? 85 C0 75 17 4C 8B C3 48 8B D7 49 8B CC E8";

// PD2's five `jmp`s, D2R's jcc in the think: pattern, offset of the jcc, its bytes
struct Branch {
	const char* name;
	const char* pattern;
	int offset;
	uint8_t from[2];
};
constexpr Branch Branches[] = {
	{"buff casts", "E8 ?? ?? ?? ?? 83 F8 01 75 41 83 7C 24 68 00 74 24", 8, {0x75, 0x41}},
	{"no owner target", "E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 74 25 48 8B C8 E8 ?? ?? ?? ?? 85 C0 75 17 4C 8B C3", 23, {0x75, 0x17}},
	{"no basic attack", "44 0F 4C F0 83 7C 24 68 00 74 73 48 8B CF", 9, {0x74, 0x73}},
	{"no crowd step", "83 FA 03 7E 6E 8B 06 48 69 C8 C5 90 C6 6A", 3, {0x7e, 0x6e}},
	{"no fallback attack", "4D 85 FF 0F 85 ?? ?? ?? ?? 4D 85 ED 74 45 45 33 C9 45 33 C0 49 8B D5 48 8B CF E8", 12, {0x74, 0x45}},
};

// The rva a `call rel32` at `rva` goes to
auto CallTarget(uint64_t exeBase, uint64_t rva) noexcept -> uint64_t {
	int32_t rel = 0;
	std::memcpy(&rel, reinterpret_cast<const void*>(exeBase + rva + 1), sizeof(rel));
	return rva + 5 + static_cast<int64_t>(rel);
}

auto Scan(const D2RL::PluginContext* context, const char* name, const char* pattern, int offset, uint64_t* rva) noexcept
	-> bool {
	const int n = sigscan::ScanUnique(context->exeBase, pattern, offset, rva);
	if (n != 1) {
		plog::Warn("summon ai: skipped %s: %d matches", name, n);
		return false;
	}
	return true;
}

auto PatchBranches(const D2RL::PluginContext* context, const uint64_t (&rvas)[std::size(Branches)]) noexcept -> bool {
	const D2RL::MutationService* mutations = nullptr;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize) ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		return false;
	}
	uint8_t to[std::size(Branches)][2];
	for (size_t i = 0; i < std::size(Branches); ++i) {
		to[i][0] = 0xeb;  // jmp rel8, same displacement
		to[i][1] = Branches[i].from[1];
		const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
		                                              .rva = rvas[i],
		                                              .expected = Branches[i].from,
		                                              .expectedSize = 2,
		                                              .bytes = to[i],
		                                              .size = 2};
		D2RL::Mutations::OperationHandle operation = 0;
		if (mutations->stageBytePatch(context, transaction, &patch, &operation) != D2RL::Mutations::Result::Success) {
			mutations->cancelTransaction(context, transaction);
			return false;
		}
	}
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		return false;
	}
	return true;
}

}  // namespace

auto InstallSummonAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	// Every address first: a patched branch would hide the dead-test call's signature
	uint64_t leash = 0, mover = 0, call1 = 0, call2 = 0, pathTarget = 0, isDead = 0, branches[std::size(Branches)] = {};
	bool found = Scan(context, "leash", LeashPattern, 0, &leash) && Scan(context, "mover call", MoverCallPattern, 19, &mover) &&
	             Scan(context, "leash call 1", LeashCall1Pattern, 0x41, &call1) &&
	             Scan(context, "leash call 2", LeashCall2Pattern, 0x35, &call2) &&
	             Scan(context, "path target call", PathTargetCallPattern, 6, &pathTarget) &&
	             Scan(context, "dead test call", IsDeadCallPattern, 3, &isDead);
	for (size_t i = 0; found && i < std::size(Branches); ++i) {
		found = Scan(context, Branches[i].name, Branches[i].pattern, Branches[i].offset, &branches[i]);
	}
	bool hooked = false, patched = false;
	if (found) {
		const uint64_t base = context->exeBase;
		g_mover = reinterpret_cast<MoverFn>(base + CallTarget(base, mover));
		g_pathTarget = reinterpret_cast<PathTargetFn>(base + CallTarget(base, pathTarget));
		g_isDead = reinterpret_cast<IsDeadFn>(base + CallTarget(base, isDead));
		g_leashReturn[0] = base + call1;
		g_leashReturn[1] = base + call2;
		plog::Info("summon ai: leash 0x%llx, mover 0x%llx, think calls return at 0x%llx and 0x%llx",
		           static_cast<unsigned long long>(leash), static_cast<unsigned long long>(CallTarget(base, mover)),
		           static_cast<unsigned long long>(call1), static_cast<unsigned long long>(call2));
		hooked = context->InstallInlineHook(leash, LeashEntry, sizeof(LeashEntry), &SummonLeash, &g_leashOriginal);
		patched = PatchBranches(context, branches);
	}
	std::snprintf(report, reportSize, "ShadowMaster leash%s; ShadowMaster branches%s",
	              !found ? " FAILED (signature)" : hooked ? "" : " FAILED (bytes differ)",
	              !found ? " FAILED (signature)" : patched ? "" : " FAILED (bytes differ)");
	return (hooked ? 1 : 0) + (patched ? 1 : 0);
}

auto SummonAiCount() noexcept -> int {
	return 2;
}

auto SummonAiCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "summon leash calls %u: stay %u, walk back %u, warp %u, wander %u, step aside %u",
	              g_leashCalls.load(), g_moves[MoveNone].load(), g_moves[MoveBack].load(), g_moves[MoveWarp].load(),
	              g_moves[MoveWander].load(), g_moves[MoveAside].load());
}
