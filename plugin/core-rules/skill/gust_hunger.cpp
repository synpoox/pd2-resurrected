// gust_hunger
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

// --- Gust, Hunger (PD2 srvdofunc 156 and 165 -> router 190; docs/research/skills/druid-w23.md) --------------------------
std::atomic<uint32_t> g_gusts {0};

std::atomic<uint32_t> g_hungers {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t Gust = 442, Hunger = 242;  // merged ids (PD2 370, 242)

// PD2 srvdo 156 (0x103b1980): the teleport core, srvmissilea thrown at the skill's target (1.13c 0xa28f0, D2R's
// ThrowSkillMissile), then a hit on every unit within calc3 of the new point with the row's HitFlags, ResultFlags and
// HitClass and cold length calc1, no damage, through 1.13c's area apply 0xa10b0: D2R's 0x436e90, which AreaKnockback's
// callback calls with a copy of the block (unit mask 0x8583, PD2's).

// PD2 srvdo 156 (0x103b1980): the teleport core, srvmissilea thrown at the skill's target (1.13c 0xa28f0, D2R's
// ThrowSkillMissile), then a hit on every unit within calc3 of the new point with the row's HitFlags, ResultFlags and
// HitClass and cold length calc1, no damage, through 1.13c's area apply 0xa10b0: D2R's 0x436e90, which AreaKnockback's
// callback calls with a copy of the block (unit mask 0x8583, PD2's).
auto SrvDoGust(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr || !TeleportCore(game, unit, skillId, level)) {
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA);
	bool thrown = false;
	if (d2r::ValidMissile(dataFlag, missile)) {
		using ThrowFn = Unit* (*)(Game*, int32_t, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
		                          int32_t) noexcept;
		const int32_t key = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(unit);
		thrown = d2r::Engine<ThrowFn>(ThrowSkillMissile)(game, missile, unit, skillId, level, key, 0, 0, 0, 0, 1) != nullptr;
	}
	const int32_t chill = d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level);
	const int32_t radius = d2r::EvalCalc(game, unit, skill, SkCalc3, skillId, level);
	d2r::LeapKnockback hit;
	hit.hitFlags = At<uint32_t>(skill, SkHitFlags);
	hit.resultFlags = At<uint16_t>(skill, SkResultFlags);
	auto* block = reinterpret_cast<uint8_t*>(&hit);
	*reinterpret_cast<int32_t*>(block + DmgColdLength) = chill;
	*reinterpret_cast<int32_t*>(block + DmgHitClass) = At<int32_t>(skill, SkHitClass);
	if (radius > 0) {
		uint8_t scratch[0x20] {};  // as Leap's landing passes it: only its byte +0x18 (0) is read
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, d2r::LeapKnockback*, int32_t, void*) noexcept>(
			AreaKnockback)(game, unit, 0, 0, radius, &hit, 0, scratch);
	}
	++g_gusts;
	return 1;
}

// PD2 srvdo 165 (0x103b27c0): the target (a corpse, TargetCorpse) gets corpse_nodraw, then the caster gets aurastate
// for auralencalc frames with aurastat1-6 = aurastatcalc1-6, through PD2's state helper 0x1036c920 (the same recast
// rule as TimedStatList's). An empty aurastat1 gives no list; a later empty one is skipped. PD2 reads the path's
// target unit, D2R's own Hunger (srvdo 122) the skill's: the same corpse.
// shortcut: an extended list's new timer is not sent to the client (PD2 sends packet 0x83), as for Teleport.
// Upgrade if the client's hunger state runs out early in play.

// PD2 srvdo 165 (0x103b27c0): the target (a corpse, TargetCorpse) gets corpse_nodraw, then the caster gets aurastate
// for auralencalc frames with aurastat1-6 = aurastatcalc1-6, through PD2's state helper 0x1036c920 (the same recast
// rule as TimedStatList's). An empty aurastat1 gives no list; a later empty one is skipped. PD2 reads the path's
// target unit, D2R's own Hunger (srvdo 122) the skill's: the same corpse.
// shortcut: an extended list's new timer is not sent to the client (PD2 sends packet 0x83), as for Teleport.
// Upgrade if the client's hunger state runs out early in play.
auto SrvDoHunger(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	Unit* corpse = skill != nullptr ? d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit) : nullptr;
	if (corpse == nullptr || corpse == unit) {
		return 0;
	}
	// A bare state: SetStateBit (the bit UnitHasState reads), then SetStateGfxFlag, as D2R's 0x56bfe0 (#623)
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateBit)(corpse, CorpseNoDraw, 1);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateGfxFlag)(corpse, CorpseNoDraw, 1);
	d2r::api.setUnitSkillFlag(unit, 1);  // PD2 sets unit flag 0x40 on every path past the corpse
	StatValue stats[6] {};
	size_t count = 0;
	for (int i = 0; i < 6; i++) {
		const int32_t stat = At<int16_t>(skill, SkAuraStat1 + i * 2);
		// shortcut: PD2 also skips a stat past itemstatcost; the merged rows name none. Upgrade if a row ever does.
		if (stat < 0) {
			if (i == 0) {
				break;
			}
			continue;
		}
		stats[count++] = {stat, d2r::EvalCalc(game, unit, skill, SkAuraStatCalc1 + i * 4, skillId, level)};
	}
	const int32_t state = At<int16_t>(skill, SkAuraState);
	const int32_t frames = d2r::EvalCalc(game, unit, skill, SkAuraLenCalc, skillId, level);
	const char* list = "none";
	if (count > 0 && state > 0 && frames > 0) {  // PD2's state helper refuses a length under 1
		list = TimedStatList(dataFlag, unit, unit, skillId, level, frames, state, stats, stats + count);
	}
	++g_hungers;
	return 1;
}

// --- Assassin charge-up finishers (PD2 srvdofunc 170 -> router 190, srvprg 171 -> 184, 172 -> 185, 39 -> 186,
// 139 -> 181; docs/research/skills/w29-assassin-finishers.md) ---------------------------------------------------------------
// Tiger Strike, Fists of Fire, Cobra Strike, Claws of Thunder, Blades of Ice, Royal Strike, RakanothStrike (merged ids)

}  // namespace

PD2R_ROUTE(32, Gust, SrvDoGust)
PD2R_ROUTE(33, Hunger, SrvDoHunger)
