// double_throw
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

// --- Double Throw (PD2 srvdofunc 154 -> 183, cltdofunc 98 -> 116) ------------------------------------------------
std::atomic<uint32_t> g_doubleThrowCasts {0};

std::atomic<uint32_t> g_doubleThrowClientCasts {0};

// PD2's column for the thrown weapon: throwing axe a, throwing knife b, javelin, glaive and pilum c, else a. Missile
// rows by weapons.txt missiletype, the same in D2R and PD2 (javelin 1, throwaxe 35, throwknife 36, glaive 37, pilum 371).

}  // namespace pd2r::skills

namespace {

// PD2's column for the thrown weapon: throwing axe a, throwing knife b, javelin, glaive and pilum c, else a. Missile
// rows by weapons.txt missiletype, the same in D2R and PD2 (javelin 1, throwaxe 35, throwknife 36, glaive 37, pilum 371).
auto WeaponColumn(Unit* weapon) noexcept -> int {
	switch (d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::ItemMissileType)(weapon)) {
	case 35: return 0;
	case 36: return 1;
	case 1: case 37: case 371: return 2;
	default: return 0;
	}
}

// PD2 srvdo 154 (ProjectDiablo.dll 0x103b1590): one missile of srvmissile a/b/c by the weapon, thrown the way D2R's
// Double Throw throws (srvdo 74's 0x4333f0), with damagepercent = calc1 and calc2 bounces (missiles.txt pSrvHitFunc 30
// -> 66 spends them). A monster (UberMadawcDoubleThrow, the same missile in all three columns) throws column a.

// PD2 srvdo 154 (ProjectDiablo.dll 0x103b1590): one missile of srvmissile a/b/c by the weapon, thrown the way D2R's
// Double Throw throws (srvdo 74's 0x4333f0), with damagepercent = calc1 and calc2 bounces (missiles.txt pSrvHitFunc 30
// -> 66 spends them). A monster (UberMadawcDoubleThrow, the same missile in all three columns) throws column a.
auto SrvDoDoubleThrow(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	int column = 0;
	if (At<int32_t>(unit, UnitType) == 0) {
		Unit* weapon = d2r::Engine<Unit* (*)(Unit*) noexcept>(UnitWeapon)(unit);
		if (weapon == nullptr) {
			return 0;
		}
		column = WeaponColumn(weapon);
	}
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA + column * 2);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		return 0;
	}
	using ThrowFn = Unit* (*)(Game*, int32_t, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
	                          int32_t) noexcept;
	const int32_t key = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(unit);
	Unit* thrown = d2r::Engine<ThrowFn>(ThrowSkillMissile)(game, missile, unit, skillId, level, key, 0, 0, 0, 0, 1);
	if (thrown == nullptr) {
		return 0;
	}
	const int32_t damage = d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level);
	const int32_t bounces = d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(thrown, StatDamagePercent, damage, 0);
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileBounces)(thrown, bounces);
	++g_doubleThrowCasts;
	return 1;
}

// PD2 cltdo 98 (0x103ab470): the same missile from cltmissile a/b/c with calc2 bounces, so the client's Chain
// Lightning hit (missiles.txt pCltHitFunc 16) can draw each bounce.

// PD2 cltdo 98 (0x103ab470): the same missile from cltmissile a/b/c with calc2 bounces, so the client's Chain
// Lightning hit (missiles.txt pCltHitFunc 16) can draw each bounce.
auto CltDoDoubleThrow(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	int column = 0;  // a monster throws column a, as on the server
	if (At<int32_t>(unit, UnitType) == 0) {
		Unit* weapon = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientUnitWeapon)(unit);
		if (weapon == nullptr) {
			return 0;
		}
		column = WeaponColumn(weapon);
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	const int32_t missile = At<int16_t>(skill, SkCltMissileA + column * 2);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		return 0;
	}
	const int32_t key = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(unit);
	Unit* thrown = d2r::Engine<Unit* (*)(Unit*, int32_t, int32_t, int32_t, int32_t) noexcept>(ClientThrowSkillMissile)(
		unit, missile, skillId, level, key);
	if (thrown == nullptr) {
		return 0;
	}
	const int32_t bounces = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, SkCalc2), skillId, level);
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileBounces)(thrown, bounces);
	++g_doubleThrowClientCasts;
	return 1;
}

// The unit's list on `state` dropped, state off and list freed, as RemoveSkillState (0x4370b0) does per state. That
// one removes by states.txt group and does nothing for a state with none (temp_skill_buff, the progressive states).

}  // namespace

PD2R_SLOT(15, SrvDo, 183, static_cast<SrvDoFn>(SrvDoDoubleThrow), "double throw")
PD2R_SLOT(16, CltDo, 116, static_cast<CltDoFn>(CltDoDoubleThrow), "double throw")
