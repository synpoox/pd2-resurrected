// walls
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

// --- Firestorm and the wall makers (PD2 srvdofunc 166 -> router 190, cltdofunc 119) ------------------------------
// Read from ProjectDiablo.dll: srvdo 166 at 0x103b29d0, cltdo 119 at 0x103acb70, their maker callback 0x1036f220
// (docs/research/skills/druid-w22.md). calc1 makers of srvmissilea (the client reads it too), all from the caster to the
// target, each with the maker callback; D2R's Firestorm (srvdo 117) sends its first maker without it and DiabWall's
// (srvdo 102) skips the path one time in five.
constexpr int32_t DiabWall = 197, Firestorm = 225, FingermageBossDiabWall = 473, UberDiabWall = 502,
                  KanemithFirestorm = 632;  // merged ids

// shortcut: PD2 seeds each maker from a counter it keeps on the caster (+0xcc, bumped per maker); D2R's callback 1
// seeds from its argument plus a path value, so each maker gets the next value of one game-wide counter. Upgrade if
// the walls' paths look less spread than PD2's in play.
// shortcut: makers are capped at 64 (PD2 has no cap; no PD2 row gets near it), upgrade if a row ever needs more.

}  // namespace

namespace pd2r::skills {

// shortcut: PD2 seeds each maker from a counter it keeps on the caster (+0xcc, bumped per maker); D2R's callback 1
// seeds from its argument plus a path value, so each maker gets the next value of one game-wide counter. Upgrade if
// the walls' paths look less spread than PD2's in play.
// shortcut: makers are capped at 64 (PD2 has no cap; no PD2 row gets near it), upgrade if a row ever needs more.
std::atomic<int32_t> g_makerSeeds {0};

}  // namespace pd2r::skills

namespace {

auto SrvDoWall(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, d2r::rva::SkSrvMissileA);
	int32_t tx = 0, ty = 0;
	if (!d2r::ValidMissile(dataFlag, missile) ||
	    d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	const int32_t count = d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc1, skillId, level);
	const int32_t created = Walls(unit, missile, count, skillId, level, tx, ty, [&](d2r::MissileParams* p) {
		return d2r::api.createMissile(game, p) != nullptr;
	});
	return 1;
}


auto CltDoWall(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, d2r::rva::SkSrvMissileA);
	int32_t tx = 0, ty = 0;
	if (!d2r::ValidMissile(dataFlag, missile) ||
	    d2r::api.clientSkillTargetXY(unit, &tx, &ty) == 0) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	const int32_t count =
		d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, d2r::rva::SkCalc1), skillId, level);
	const int32_t created = Walls(unit, missile, count, skillId, level, tx, ty, [](d2r::MissileParams* p) {
		return d2r::api.clientCreateMissile(p, 0) != nullptr;
	});
	return 1;
}

// --- Fire Claws (PD2 srvdofunc 175, cltdofunc 110; docs/research/skills/druid-w24.md) ------------------------------------
// Read from ProjectDiablo.dll: srvdo 175 at 0x103b3ad0, cltdo 110 at 0x103abbd0. A claw hit on the skill's target
// unit, then calc2 makers of srvmissilea (the client: cltmissilea, aimed at the target unit) with Firestorm's maker
// callback. The hit is D2R's Fire Claws hit: its start (srvst 58, kept as data) rolls and queues it with the skill's
// fire damage and srvdo 2 applies it; PD2 puts the fire on its start's buff (srvst 79) and rolls the hit here.

}  // namespace

PD2R_SLOT(40, CltDo, 119, static_cast<CltDoFn>(CltDoWall), "wall")
PD2R_ROUTE(41, Firestorm, SrvDoWall)
PD2R_ROUTE(42, DiabWall, SrvDoWall)
PD2R_ROUTE(43, UberDiabWall, SrvDoWall)
PD2R_ROUTE(44, FingermageBossDiabWall, SrvDoWall)
PD2R_ROUTE(45, KanemithFirestorm, SrvDoWall)
