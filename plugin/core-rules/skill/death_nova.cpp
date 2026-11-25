// death_nova
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

// --- Death novas (PD2 srvdofunc 169 -> router 190, cltdofunc 105 -> 125) ---------------------------------------------
// Read from ProjectDiablo.dll: srvdo 169 at 0x103b2bc0, cltdo 105 at 0x103ab880 (#480). Each
// half creates one srvmissilea at the caster (flags 1, no target); the client half reads srvmissilea too, not
// cltmissilea, so NihlathakMapDeath (no srvmissilea) draws nothing, as in PD2.
constexpr int32_t DollMeteor = 435, RathmaDeath = 554, LucionDeath1 = 649, LucionDeath2 = 650, LucionDeath3 = 651,
                  LucionDeath4 = 652;  // merged ids


auto SrvDoDeathNova(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	int32_t x = 0, y = 0;
	if (skill == nullptr || !d2r::ValidMissile(dataFlag, At<int16_t>(skill, SkSrvMissileA)) ||
	    !d2r::UnitXY(unit, &x, &y)) {
		return 0;
	}
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY, At<int16_t>(skill, SkSrvMissileA), x, y, skillId,
	                                               level);
	const bool created = d2r::api.createMissile(game, &params) != nullptr;
	return created ? 1 : 0;
}


auto CltDoDeathNova(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	int32_t x = 0, y = 0;
	if (skill == nullptr || !d2r::ValidMissile(dataFlag, At<int16_t>(skill, SkSrvMissileA)) ||
	    !d2r::UnitXY(unit, &x, &y)) {
		return 0;
	}
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY, At<int16_t>(skill, SkSrvMissileA), x, y, skillId,
	                                               level);
	const bool created = d2r::api.clientCreateMissile(&params, 0) != nullptr;
	return created ? 1 : 0;
}

// --- Static Field (PD2 srvdofunc 160 -> router 190) ------------------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 160 at 0x103b1dc0, per-unit damage 0x10302830, per-unit debuff 0x1036c920
// (docs/research/skills/w27-static-field-combustion.md). Two passes over the units within aurarangecalc that aurafilter
// takes: lightning damage from current life (StaticFieldDamage), then auratargetstate with aurastat1-6 for
// auralencalc frames.

}  // namespace

PD2R_SLOT(39, CltDo, 125, static_cast<CltDoFn>(CltDoDeathNova), "death nova")
PD2R_ROUTE(20, DollMeteor, SrvDoDeathNova)
PD2R_ROUTE(21, RathmaDeath, SrvDoDeathNova)
PD2R_ROUTE(22, LucionDeath1, SrvDoDeathNova)
PD2R_ROUTE(23, LucionDeath2, SrvDoDeathNova)
PD2R_ROUTE(24, LucionDeath3, SrvDoDeathNova)
PD2R_ROUTE(25, LucionDeath4, SrvDoDeathNova)
