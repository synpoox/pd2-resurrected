// split_throw
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

std::atomic<uint32_t> g_splitThrowCasts {0};

std::atomic<uint32_t> g_splitThrowClientCasts {0};

// Which of the skill's missile columns a/b/c (0-2) the caster's weapon picks; the server and client use the same one.

}  // namespace pd2r::skills

namespace {

// Which of the skill's missile columns a/b/c (0-2) the caster's weapon picks; the server and client use the same one.
auto MissileColumn(Unit* unit, int32_t skillId, const uint8_t* skill) noexcept -> int {
	const int32_t row = d2r::api.pickSkillMissile(unit, skillId);
	for (int column = 1; column < 3; ++column) {
		if (row >= 0 && row == At<int16_t>(skill, d2r::rva::SkSrvMissileA + column * 2)) {
			return column;
		}
	}
	return 0;
}


}  // namespace

namespace pd2r::skills {

auto ClampCount(int32_t count) noexcept -> int32_t {
	return count < 1 ? 1 : (count > 64 ? 64 : count);  // a broken formula must not spawn thousands of missiles
}

// Split Throw (PD2 srvdofunc 189): calc1 missiles of srvmissile a/b/c (by weapon) in Multiple Shot's line, calc2
// activation frame, calc3 triggering missiles. PD2's calc1 is par1+min(lvl/5,par2).
// shortcut: calc4 (PD2's ED%) is not applied; Multiple Shot takes its calc4 through create callback 4 instead (#1056).
// Upgrade when PD2's srvdo 189 is read from ProjectDiablo.dll.

}  // namespace pd2r::skills

namespace {

// Split Throw (PD2 srvdofunc 189): calc1 missiles of srvmissile a/b/c (by weapon) in Multiple Shot's line, calc2
// activation frame, calc3 triggering missiles. PD2's calc1 is par1+min(lvl/5,par2).
// shortcut: calc4 (PD2's ED%) is not applied; Multiple Shot takes its calc4 through create callback 4 instead (#1056).
// Upgrade when PD2's srvdo 189 is read from ProjectDiablo.dll.
auto SrvDoSplitThrow(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);  // every D2R do function that creates missiles does this first

	int32_t tx = 0, ty = 0, ux = 0, uy = 0;
	if (d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0) {
		return 1;  // no target point: the cast happened, nothing to create
	}
	if (!d2r::UnitXY(unit, &ux, &uy)) {
		return 0;
	}
	const int column = MissileColumn(unit, skillId, skill);
	const int32_t missile = At<int16_t>(skill, d2r::rva::SkSrvMissileA + column * 2);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		if (static std::atomic<bool> logged {false}; !logged.exchange(true)) {  // bad table data repeats every cast
			plog::Warn("split throw: skill %d has no valid srvmissile column %d (row %d)", skillId, column, missile);
		}
		return 0;
	}
	const int32_t count = ClampCount(d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc1, skillId, level));

	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.owner = unit;
	params.origin = unit;
	params.missile = missile;
	params.skill = skillId;
	params.skillLevel = level;
	params.activation = d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc2, skillId, level);
	const int32_t triggered = d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc3, skillId, level);
	const int32_t created = pd2::line_spread::Spread(ux, uy, tx, ty, count, triggered <= 0 ? count : triggered,
	                                                 [&](int32_t x, int32_t y, bool outer) {
		params.flags = d2r::rva::MissileFromOrigin | (outer ? d2r::rva::MissileNotTriggering : 0);
		params.targetX = x;
		params.targetY = y;
		return d2r::api.createMissile(game, &params) != nullptr;
	});
	++g_splitThrowCasts;
	return 1;
}

// Multiple Shot (PD2 srvdofunc 8, #1056): Split Throw's line with PD2's rules. calc1 missiles, calc2 activation frame,
// calc3 triggering missiles (at most calc1; 0 = none, so Multiple Shot Proc's arrows trigger nothing). calc4 is the
// missiles' damage % (stat 25). D2R's srvdo 8 reads calc5 for that, so calc4 rides on create callback 4 instead.

// Multiple Shot (PD2 srvdofunc 8, #1056): Split Throw's line with PD2's rules. calc1 missiles, calc2 activation frame,
// calc3 triggering missiles (at most calc1; 0 = none, so Multiple Shot Proc's arrows trigger nothing). calc4 is the
// missiles' damage % (stat 25). D2R's srvdo 8 reads calc5 for that, so calc4 rides on create callback 4 instead.
auto SrvDoMultipleShot(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);

	int32_t tx = 0, ty = 0, ux = 0, uy = 0;
	if (d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0) {
		return 1;  // no target point: the cast happened, nothing to create
	}
	if (!d2r::UnitXY(unit, &ux, &uy)) {
		return 0;
	}
	const int column = MissileColumn(unit, skillId, skill);
	const int32_t missile = At<int16_t>(skill, d2r::rva::SkSrvMissileA + column * 2);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		if (static std::atomic<bool> logged {false}; !logged.exchange(true)) {  // bad table data repeats every cast
			plog::Warn("multiple shot: skill %d has no valid srvmissile column %d (row %d)", skillId, column, missile);
		}
		return 0;
	}
	const int32_t count = ClampCount(d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc1, skillId, level));
	const int32_t triggered = std::clamp(d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc3, skillId, level), 0, count);
	const int32_t bonus = d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc4, skillId, level);

	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.owner = unit;
	params.origin = unit;
	params.missile = missile;
	params.skill = skillId;
	params.skillLevel = level;
	params.activation = d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc2, skillId, level);
	if (bonus != 0) {
		params.callback = d2r::rva::MissileCallbackDamagePercent;
		params.callbackArg = bonus;
	}
	const int32_t created = pd2::line_spread::Spread(ux, uy, tx, ty, count, triggered,
	                                                 [&](int32_t x, int32_t y, bool outer) {
		params.flags = d2r::rva::MissileFromOrigin | (outer ? d2r::rva::MissileNotTriggering : 0);
		params.targetX = x;
		params.targetY = y;
		return d2r::api.createMissile(game, &params) != nullptr;
	});
	return 1;
}

// Split Throw client half (PD2 cltdofunc 118): the same missiles from cltmissile a/b/c, so the caster sees them
// without the server sending each one (missiles.txt ClientSend).

// Split Throw client half (PD2 cltdofunc 118): the same missiles from cltmissile a/b/c, so the caster sees them
// without the server sending each one (missiles.txt ClientSend).
auto CltDoSplitThrow(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);

	int32_t tx = 0, ty = 0, ux = 0, uy = 0;
	if (d2r::api.clientSkillTargetXY(unit, &tx, &ty) == 0 || !d2r::UnitXY(unit, &ux, &uy)) {
		return 1;
	}
	const int column = MissileColumn(unit, skillId, skill);
	const int32_t missile = At<int16_t>(skill, d2r::rva::SkCltMissileA + column * 2);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		if (static std::atomic<bool> logged {false}; !logged.exchange(true)) {  // bad table data repeats every cast
			plog::Warn("split throw client: skill %d has no valid cltmissile column %d (row %d)", skillId, column, missile);
		}
		return 0;
	}
	const int32_t count = ClampCount(d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, d2r::rva::SkCalc1),
	                                                         skillId, level));

	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = d2r::rva::MissileFromOrigin;  // cltdo 17 sets no NotTriggering: hits are the server's
	params.owner = unit;
	params.origin = unit;
	params.missile = missile;
	params.skill = skillId;
	params.skillLevel = level;
	params.activation = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, d2r::rva::SkCalc2), skillId, level);
	const int32_t created = pd2::line_spread::Spread(ux, uy, tx, ty, count, count, [&](int32_t x, int32_t y, bool) {
		params.targetX = x;
		params.targetY = y;
		return d2r::api.clientCreateMissile(&params, 0) != nullptr;
	});
	++g_splitThrowClientCasts;
	return 1;
}

// --- Double Throw (PD2 srvdofunc 154 -> 183, cltdofunc 98 -> 116) ------------------------------------------------

constexpr int32_t MultipleShot = 12, MultipleShotProc = 675, TraitorBossStunSpear = 537;  // merged ids (#1056)

}  // namespace

PD2R_SLOT(0, SrvDo, 189, static_cast<SrvDoFn>(SrvDoSplitThrow), "split throw")
PD2R_SLOT(1, CltDo, 118, static_cast<CltDoFn>(CltDoSplitThrow), "split throw")
PD2R_ROUTE(46, MultipleShot, SrvDoMultipleShot)
PD2R_ROUTE(47, MultipleShotProc, SrvDoMultipleShot)
PD2R_ROUTE(48, TraitorBossStunSpear, SrvDoMultipleShot)
