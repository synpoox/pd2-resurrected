// summon_think
#include "common.h"
#include "registry.h"

#include "ai_registry.h"
#include "../item_rules.h"
#include "line_spread.h"
#include "log.h"
#include "../missile_functions.h"
#include "parry_roll.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"
#include "skill_router.h"
#include "../skill_functions.h"
#include "../core-events/arena_spots.h"

#include <D2RLPlugin/mutations.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <span>

using namespace pd2r::skills;

namespace {

// --- Summon first think and revive size (#1835) --------------------------------------------------------------------
// PD2's patch table: 1.13c D2Game +0x4d879 `14` -> `08` (srvdo 16 Valkyrie, used by no other skill: pet AI event at
// frame + 8, not + 20) and +0x50ad1 `0f` -> `08` (Revive's pet, srvdo 58, + 8, not + 15). D2R 93847 keeps both, in
// 0x558000 and 0x55e7e0 (`add r9d,imm8` before the AI event queue 0x48b720, type 2). No SDK service sets a pet's first
// think, so a MutationService byte patch changes each imm8, found by signature, expected byte checked.
constexpr const char* ValkyrieThinkDelay = "44 8b 8e 70 01 00 00 41 b8 02 00 00 00 44 89 6c 24 30 41 83 c1 14";

constexpr const char* ReviveThinkDelay = "44 8b 8e 70 01 00 00 33 c0 89 44 24 30 41 83 c1 0f";
// #1988: PD2's patch table also has 1.13c +0xa37b7 `19` -> `06`, the shared summon creator (+0xa3610) queuing a new
// pet's AI event at frame + 6, not + 25. D2R's creator is 0x4350e0 (`add r9d,0x19` at 0x435444). Valkyrie's do calls
// it, then drops that event and queues its own (+ 8 above), as in 1.13c; Revive does not call it.

// #1988: PD2's patch table also has 1.13c +0xa37b7 `19` -> `06`, the shared summon creator (+0xa3610) queuing a new
// pet's AI event at frame + 6, not + 25. D2R's creator is 0x4350e0 (`add r9d,0x19` at 0x435444). Valkyrie's do calls
// it, then drops that event and queues its own (+ 8 above), as in 1.13c; Revive does not call it.
constexpr const char* SummonThinkDelay = "44 8b 8d 70 01 00 00 41 b8 02 00 00 00 44 89 64 24 30 41 83 c1 19";
// PD2 0x102fb2b0 wraps Revive's last D2Common #10704 (collision refresh): with state 96 (revive) on, path +0x44 (unit
// size) above 2 becomes 2, then the refresh. D2R's Revive ends with the same refresh, 0x3429b0 (reads path +0x58, writes
// the collision class to +0x5c) and no cap; SrvDoRevive caps after it and refreshes again. No SDK service sets a size.

// PD2 0x102fb2b0 wraps Revive's last D2Common #10704 (collision refresh): with state 96 (revive) on, path +0x44 (unit
// size) above 2 becomes 2, then the refresh. D2R's Revive ends with the same refresh, 0x3429b0 (reads path +0x58, writes
// the collision class to +0x5c) and no cap; SrvDoRevive caps after it and refreshes again. No SDK service sets a size.
constexpr const char* CollisionRefreshEntry =
    "48 89 5c 24 08 57 48 83 ec 20 48 8b f9 e8 ?? ?? ?? ?? 48 8b d8 48 85 c0 74 ?? 48 8b cf e8 ?? ?? ?? ?? 48 8b cf "
    "48 85 c0 74 ?? ba 01 00 00 00 e8 ?? ?? ?? ?? 8b 53 58";

constexpr int PathUnitSize = 0x58;  // uint32 on a dynamic path (1.13c +0x44)

uint64_t g_collisionRefresh = 0;  // rva, 0 when the signature did not match once


static auto PatchThinkDelay(const D2RL::PluginContext* context, const char* pattern, int at, uint8_t stock,
                            uint8_t delay, const char* what) noexcept -> bool {
	uint64_t rva = 0;
	const D2RL::MutationService* mutations = nullptr;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (sigscan::ScanUnique(context->exeBase, pattern, at, &rva) != 1 ||
	    context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize) ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		plog::Info("summon think: %s first think FAILED (signature or mutation service)", what);
		return false;
	}
	const uint8_t PD2Delay[] = {delay};
	const uint8_t expected[] = {stock};
	const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
	                                              .rva = rva,
	                                              .expected = expected,
	                                              .expectedSize = sizeof(expected),
	                                              .bytes = PD2Delay,
	                                              .size = sizeof(PD2Delay)};
	D2RL::Mutations::OperationHandle operation = 0;
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->stageBytePatch(context, transaction, &patch, &operation) != D2RL::Mutations::Result::Success ||
	    mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		plog::Info("summon think: %s first think FAILED (byte at 0x%llx is not 0x%02x)", what,
		           static_cast<unsigned long long>(rva), stock);
		return false;
	}
	plog::Info("summon think: %s first think at frame + %u (was + %u) at 0x%llx", what, delay, stock,
	           static_cast<unsigned long long>(rva));
	return true;
}


static void CapReviveSize(Unit* pet) noexcept {
	auto* path = At<uint8_t*>(pet, d2r::rva::UnitPath);
	if (g_collisionRefresh == 0 || path == nullptr ||
	    d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(d2r::rva::UnitHasState)(pet, StateRevive) == 0) {
		return;
	}
	auto* size = reinterpret_cast<uint32_t*>(path + PathUnitSize);
	const uint32_t before = *size;
	if (before <= 2) {
		return;
	}
	*size = 2;
	d2r::Engine<void (*)(Unit*) noexcept>(g_collisionRefresh)(pet);
}


}  // namespace

namespace pd2r::skills {

auto SrvDoRevive(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	Unit* target = game != nullptr && unit != nullptr
	                   ? d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(d2r::rva::SkillTargetUnit)(game, unit)
	                   : nullptr;
	const int64_t result = d2r::Engine<SrvDoFn>(d2r::rva::SrvDoReviveD2R)(game, unit, skillId, level);
	if (result == 0 || target == nullptr || At<int32_t>(target, d2r::rva::UnitType) != d2r::rva::UnitMonster) {
		return result;
	}
	const int32_t mode = At<int32_t>(target, d2r::rva::UnitMode);
	if (mode == d2r::rva::MonModeDeath || mode == d2r::rva::MonModeDead) {
		return result;
	}
	d2r::Engine<SrvDoFn>(d2r::rva::RegisterSkillEvents)(game, target, skillId, level);
	CapReviveSize(target);
	++g_revives;
	return result;
}

// --- Frozen Armor and Enchant on allies (PD2 srvdofunc 25, #1870) ----------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 25 at 0x103aec40 (installed 0x103aecb0), its per-unit callback 0x1036c920.
// D2R's srvdo 25 (0x553650) puts aurastate on one unit (the skill's target, else the caster). PD2's builds
// {skill, level, auralencalc, auratargetstate, aurastat1-6, their aurastatcalc values, divisor 2}, runs the callback
// on the caster, then on every other unit within aurarangecalc that aurafilter takes (D2Game's area search 0xa0c70,
// D2R's AreaUnits, which skips the caster; flag 1). The callback halves each value (a tenth for enchant/coldenchant on
// a revive) for another player, a hireling or a unit whose flags +0xc4 (D2R +0x124) have the top bit; the caster and
// its other pets get the full value. The row's own tooltip says allies get half.

}  // namespace pd2r::skills

auto InstallSummonThink(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	size_t used = std::strlen(report);
	const bool valkyrieThink = PatchThinkDelay(context, ValkyrieThinkDelay, 21, 0x14, 0x08, "valkyrie");
	const bool reviveThink = PatchThinkDelay(context, ReviveThinkDelay, 16, 0x0f, 0x08, "revive");
	const bool summonThink = PatchThinkDelay(context, SummonThinkDelay, 21, 0x19, 0x06, "summon");
	plog::Info("summon: first think 6 %s", summonThink ? "on" : "FAILED");
	if (sigscan::ScanUnique(context->exeBase, CollisionRefreshEntry, 0, &g_collisionRefresh) != 1) {
		g_collisionRefresh = 0;
	}
	plog::Info("revive: size cap %s", g_collisionRefresh != 0 ? "on" : "FAILED (collision refresh signature)");
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; first think valkyrie%s revive%s summon%s, revive size cap%s",
		              valkyrieThink ? "" : " FAILED", reviveThink ? "" : " FAILED", summonThink ? "" : " FAILED",
		              g_collisionRefresh != 0 ? "" : " FAILED");
	}
	int installed = 0;
	for (const bool ok : {valkyrieThink, reviveThink, summonThink, g_collisionRefresh != 0}) {
		installed += ok ? 1 : 0;
	}
	return installed;
}

PD2R_RULE_N(InstallSummonThink, -70, 4)

