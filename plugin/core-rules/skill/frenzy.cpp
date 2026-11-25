// frenzy
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

// Frenzy's stacks (#1831). PD2 (ProjectDiablo.dll 0x10306780 in PD2.gpr, 0x10306790 installed) replaces 1.13c's srvdo 9
// helper (D2Game +0x28e70, called at +0x2966e and +0x296af): skill_frenzy (169) = min(current + 1, calc4), stat 351
// = the skill level, and each aurastat SET to calc(level) * stacks / max(calc4, 1) (0x1037c4c0), attackrate's value
// also on other_animrate. D2R's srvdo 9 (93847: 0x567e80) calls its helper (0x566d60) at 0x567fd6: stacks capped by
// the level, aurastats ADDED by 0x574480 at the stack count as level. The redirect calls D2R's helper first (its list
// create, timer, state event and client update stay) and then writes PD2's values over the list. PD2's monster
// frenzies are 1.13c srvdo 109 (+0xb6790), whose call of the helper (+0xb6828) PD2 leaves stock, so monsters on a
// calc4-blank row keep D2R's. Berserk (calc4 blank) gets its aurastats at the skill level with no stacks, as its
// tooltip says; PD2's cap would zero them (#1900, docs/deviations.md).
// No SDK service runs a skill's per-hit aura stack, so this redirects the one call by signature.
constexpr const char* FrenzyHelperCall = "45 8b cf 48 89 74 24 20 45 8b c4 48 8b d3 48 8b cd e8 ?? ?? ?? ?? b8 01 00 00 00";

constexpr uint32_t FrenzyHelperCallAt = 17;

constexpr int32_t ModifierListLevel = 351, StatAttackRate = 68, StatOtherAnimRate = 69;

constexpr int TablesStatCount = 0x1260;  // uint64 itemstatcost row count in DataTables, as 0x574480 reads it

using FrenzyHelperFn = int64_t (*)(Game*, Unit*, int32_t, int32_t, void*) noexcept;

FrenzyHelperFn g_frenzyHelper = nullptr;
// (game, unit, event +0x24, event +0x28): 93847 0x490ef0, the attack event reschedule D2R's helper ends with

// (game, unit, event +0x24, event +0x28): 93847 0x490ef0, the attack event reschedule D2R's helper ends with
constexpr const char* AttackRescheduleEntry =
    "48 89 5c 24 20 55 56 57 41 54 41 55 41 56 41 57 48 83 ec 60 8b a9 70 01 00 00 45 8b e1 41 8b d8 45 33 c9 45 33 c0 "
    "48 8b f2 4c 8b f9 e8";

using AttackRescheduleFn = void (*)(Game*, Unit*, int32_t, int32_t) noexcept;

AttackRescheduleFn g_attackReschedule = nullptr;

}  // namespace

namespace pd2r::skills {

std::atomic<uint32_t> g_frenzyHits {0};
// Every swing (#1904): 1.13c's srvdo 9 (+0x29600) calls the helper at +0x2966e / +0x296af before the attack, with no
// hit test, and PD2's helper reads no hit either (its exits are null checks). D2R's srvdo 9 calls it at 0x567fd6 only
// when its attack (0x568010, called at 0x567fbc) returns the hit roll's bit 0 (0x44b720: 0 on a miss, block or evade).
// No SDK service gates it, so the attack call is redirected and returns non-zero for a player's swing.

}  // namespace pd2r::skills

namespace {

// Every swing (#1904): 1.13c's srvdo 9 (+0x29600) calls the helper at +0x2966e / +0x296af before the attack, with no
// hit test, and PD2's helper reads no hit either (its exits are null checks). D2R's srvdo 9 calls it at 0x567fd6 only
// when its attack (0x568010, called at 0x567fbc) returns the hit roll's bit 0 (0x44b720: 0 on a miss, block or evade).
// No SDK service gates it, so the attack call is redirected and returns non-zero for a player's swing.
constexpr const char* FrenzySwingCall =
    "e8 ?? ?? ?? ?? 85 c0 74 ?? 45 8b cf 48 89 74 24 20 45 8b c4 48 8b d3 48 8b cd e8 ?? ?? ?? ?? b8 01 00 00 00";

using FrenzySwingFn = int32_t (*)(Game*, Unit*, Unit*, int32_t, int32_t) noexcept;

FrenzySwingFn g_frenzySwing = nullptr;

// --- Dragon Talon kick damage (#1862) ------------------------------------------------------------------------------
// PD2 patches 1.13c's Dragon Talon hit builder (D2Game +0x92bb0, srvdo 42's; only Dragon Talon has srvdo 42): at
// +0x92c5a the kick's damage% Param1 + (lvl-1) x Param2 (record +0x148 / +0x14c) becomes calc2 (+0x13c) through
// 0x10302290, and at +0x92c19 the last kick's result flags | 0xc (get-hit + knockback) become | 0x4 (get-hit only).
// D2R 93847's builder 0x572eb0 (from srvdo 42 0x570420) is 1.13c's: Param1/Param2 (+0x1b8 / +0x1bc) into the damage
// block's +0x1c, then the fill 0x573af0 (game, block, attacker, target, skill, level). No SDK service builds a skill
// hit, so the fill call is redirected by signature (calc2 written over the block's damage%) and the OR's imm8 patched.

auto FrenzyHelper(Game* game, Unit* unit, int32_t skillId, int32_t level, void* event) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = game != nullptr ? d2r::DataFlag(game) : 0;
	const uint8_t* skill = game != nullptr ? d2r::api.getSkillRecord(dataFlag, skillId) : nullptr;
	const int32_t calc4 = skill != nullptr ? d2r::EvalCalc(game, unit, skill, SkCalc4, skillId, level) : 0;
	// calc4 blank (Berserk, #1900): each aurastat at the skill level, no stacks, as its tooltip says. Monster frenzies
	// (srvdo 9, calc4 blank) keep D2R's ramp by the stack count.
	if (unit == nullptr || (calc4 <= 0 && At<int32_t>(unit, UnitType) == UnitMonster)) {
		return g_frenzyHelper(game, unit, skillId, level, event);
	}
	const int32_t cap = std::max(calc4, 1);
	const auto stateList = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList);
	const auto listStat = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue);
	const auto setStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	const int32_t state = At<int16_t>(skill, SkAuraState);
	if (tables == nullptr || state < 0 || At<uint64_t>(tables, TablesStateCount) <= static_cast<uint64_t>(state)) {
		return g_frenzyHelper(game, unit, skillId, level, event);
	}
	void* before = stateList(unit, state);
	const int32_t current = before != nullptr ? listStat(dataFlag, before, SkillFrenzyStat, 0) : 0;
	const int64_t result = g_frenzyHelper(game, unit, skillId, level, event);
	void* list = stateList(unit, state);
	if (list == nullptr) {
		return result;
	}
	const int32_t stacks = std::min(current + 1, cap);
	setStat(dataFlag, list, SkillFrenzyStat, stacks, 0);
	setStat(dataFlag, list, ModifierListLevel, level, 0);
	const uint64_t statCount = At<uint64_t>(tables, TablesStatCount);
	++g_frenzyHits;
	for (int i = 0; i < 6; ++i) {
		const int32_t stat = At<int16_t>(skill, SkAuraStat1 + 2 * i);
		if (stat < 0 || static_cast<uint64_t>(stat) >= statCount) {
			continue;
		}
		const double scaled = static_cast<double>(d2r::EvalCalc(game, unit, skill, SkAuraStatCalc1 + 4 * i, skillId, level)) *
		                      stacks / cap;
		const int32_t value = scaled <= 2147483647.0 ? static_cast<int32_t>(scaled) : INT32_MAX;  // as 0x1037c4c0
		setStat(dataFlag, list, stat, value, 0);
		if (stat == StatAttackRate) {
			setStat(dataFlag, list, StatOtherAnimRate, value, 0);
		}
	}
	// D2R's helper ends with this reschedule (0x490ef0 clears the unit's pending attack events, then queues them from
	// the current anim rate), before the SET above; PD2's refresh (1.13c 0x47a10) runs after its final stats, so again.
	if (g_attackReschedule != nullptr && event != nullptr) {
		g_attackReschedule(game, unit, At<int32_t>(event, 0x24), At<int32_t>(event, 0x28));
	}
	return result;
}

// The return only gates the helper. Monster frenzies with calc4 blank keep D2R's hit gate (FrenzyHelper keeps D2R's
// ramp for them too); the merc's and InvaderBarbarian's Frenzy (calc4 set) charge per swing as in PD2. A missing
// skill row keeps 0 (the helper reads it).

// The return only gates the helper. Monster frenzies with calc4 blank keep D2R's hit gate (FrenzyHelper keeps D2R's
// ramp for them too); the merc's and InvaderBarbarian's Frenzy (calc4 set) charge per swing as in PD2. A missing
// skill row keeps 0 (the helper reads it).
auto FrenzySwing(Game* game, Unit* unit, Unit* target, int32_t skillId, int32_t level) noexcept -> int32_t {
	const int32_t hit = g_frenzySwing(game, unit, target, skillId, level);
	using namespace d2r::rva;
	if (hit != 0 || game == nullptr || unit == nullptr) {
		return hit;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr) {
		return 0;
	}
	if (At<int32_t>(unit, UnitType) == UnitMonster && d2r::EvalCalc(game, unit, skill, SkCalc4, skillId, level) <= 0) {
		return hit;
	}
	return 1;
}

// --- Blood Golem's heal per hit (#2001) ------------------------------------------------------------------------------
// PD2 writes its own aura event 23 (ProjectDiablo.dll 0x10346f20, PD2.gpr; 0x10346f30 installed) over 1.13c's
// (D2Game +0x4e5f0) in the event table (+0x1077a8). Its rule: the target is a player or a monster (a monster with
// monstats Drain 0 for the game's difficulty is skipped, but the drain does not scale the heal), the target's
// alignment (D2Common #10830) is 0, golem and owner (0x103749f0(golem, 1); no owner: the golem itself) are alive; then
// h = calc2 (skill +0x13c) at the event's level for the owner, and the golem and the owner each get h life (1/256
// units, clipped at max life, 1.13c's +0x4df20), with overlay 151 on each one that gained life. The damage dealt is not
// read. D2R 93847's event 23 (0x55b2d0) evaluates calc2 and calc3 for the owner (or the golem) and calls 0x559ee0,
// which heals min(damage x drain%, target life) x calc2% to the golem and x calc3% to the owner; core-rules redirects
// that call. No SDK service runs a skill event, so the call is redirected.

}  // namespace

auto InstallFrenzy(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	size_t used = std::strlen(report);
	uint64_t reschedule = 0;
	if (sigscan::ScanUnique(context->exeBase, AttackRescheduleEntry, 0, &reschedule) == 1) {
		g_attackReschedule = reinterpret_cast<AttackRescheduleFn>(context->exeBase + reschedule);
	}
	const bool frenzy = g_attackReschedule != nullptr &&
	                    RedirectCall(context, FrenzyHelperCall, FrenzyHelperCallAt, reinterpret_cast<uint64_t>(&FrenzyHelper),
	                                 reinterpret_cast<uint64_t*>(&g_frenzyHelper));
	plog::Info("frenzy: PD2 stacks %s", frenzy ? "on" : "FAILED (srvdo 9 helper call or attack reschedule signature)");
	const bool swing = frenzy && RedirectCall(context, FrenzySwingCall, 0, reinterpret_cast<uint64_t>(&FrenzySwing),
	                                          reinterpret_cast<uint64_t*>(&g_frenzySwing));
	plog::Info("frenzy: every swing %s", swing ? "on" : "FAILED (srvdo 9 attack call signature)");
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; frenzy stacks%s, every swing%s", frenzy ? "" : " FAILED",
		              swing ? "" : " FAILED");
	}
	int installed = 0;
	for (const bool ok : {frenzy, swing}) {
		installed += ok ? 1 : 0;
	}
	return installed;
}

PD2R_RULE_N(InstallFrenzy, -60, 2)

