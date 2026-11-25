// splash_start
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

namespace pd2r::skills {

std::atomic<uint32_t> g_splashStarts {0};


}  // namespace pd2r::skills

namespace {

// Frenzy / Tiger Strike start (PD2 srvstfunc 78): inc_splash_radius on a temp_skill_buff list, from calc2 and the
// skill's charges. Frenzy: calc2 * skill_frenzy / max(calc4, 1). Tiger Strike: calc2 * charges while under 3, else 0.
// Other skills: calc2. Merged ids (ADR 0003): state temp_skill_buff 270, stats skill_frenzy 169, inc_splash_radius 531.
// shortcut: PD2 keeps one permanent list and updates it; this replaces the list with a SplashFrames one each start,
// which also drops Split Throw's list on the same state (srvst 79) and the reverse until the next Frenzy swing.
// Upgrade (find D2R's get-state-list function) if a splash effect needs the list to outlive a long pause.
constexpr int32_t Frenzy = 147;

auto SrvStSplash(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	if (skill == nullptr || tables == nullptr || At<uint64_t>(tables, TablesStateCount) <= TempSkillBuff) {
		return 1;  // not the merged data: the attack goes on without the stat
	}
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const int32_t calc2 = d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level);
	int32_t charges = 0, value = calc2;
	if (skillId == Frenzy) {
		const int32_t calc4 = d2r::EvalCalc(game, unit, skill, SkCalc4, skillId, level);
		charges = unitStat(unit, SkillFrenzyStat, 0);
		value = calc2 * charges / (calc4 > 1 ? calc4 : 1);
	} else if (skillId == TigerStrike) {
		charges = unitStat(unit, At<int16_t>(skill, SkAuraStat1), 0);
		value = charges < 3 ? calc2 * charges : 0;
	}
	DropStateList(dataFlag, unit, TempSkillBuff);  // always a new list
	TimedStatList(dataFlag, unit, unit, skillId, level, SplashFrames, TempSkillBuff, {{IncSplashRadius, value}});
	++g_splashStarts;
	return 1;
}

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

}  // namespace

PD2R_SLOT(3, SrvSt, 78, static_cast<SrvDoFn>(SrvStSplash), "splash start")
