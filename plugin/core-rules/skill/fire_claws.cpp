// fire_claws
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

// --- Fire Claws (PD2 srvdofunc 175, cltdofunc 110; docs/research/skills/druid-w24.md) ------------------------------------
// Read from ProjectDiablo.dll: srvdo 175 at 0x103b3ad0, cltdo 110 at 0x103abbd0. A claw hit on the skill's target
// unit, then calc2 makers of srvmissilea (the client: cltmissilea, aimed at the target unit) with Firestorm's maker
// callback. The hit is D2R's Fire Claws hit: its start (srvst 58, kept as data) rolls and queues it with the skill's
// fire damage and srvdo 2 applies it; PD2 puts the fire on its start's buff (srvst 79) and rolls the hit here.
std::atomic<uint32_t> g_fireClaws {0};

std::atomic<uint32_t> g_fireClawHits {0};

std::atomic<uint32_t> g_fireClawClients {0};


}  // namespace pd2r::skills

namespace {

auto SrvDoFireClaws(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	const uint8_t* hit =
		target != nullptr ? d2r::Engine<uint8_t* (*)(Unit*, Unit*) noexcept>(QueuedHit)(unit, target) : nullptr;
	const int32_t result = hit != nullptr ? At<uint16_t>(hit, DamageResult) : -1;
	// whatever the start queued is applied, as D2R's own row does
	VanillaSlot<SrvDoFn>(SrvDoTable, SrvDoMelee)(game, unit, skillId, level);
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	if (target == nullptr) {
		++g_fireClaws;
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA);
	int32_t tx = 0, ty = 0;
	const bool aimed = d2r::ValidMissile(dataFlag, missile) && d2r::api.skillTargetXY(game, unit, &tx, &ty) != 0;
	const int32_t count = aimed ? d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level) : 0;
	const int32_t created = Walls(unit, missile, count, skillId, level, tx, ty, [&](d2r::MissileParams* p) {
		return d2r::api.createMissile(game, p) != nullptr;
	});
	++g_fireClaws;
	if (result >= 0 && (result & 1) != 0) {
		++g_fireClawHits;
	}
	return aimed ? 1 : 0;
}


auto CltDoFireClaws(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	Unit* target = d2r::Engine<Unit* (*)(Unit*) noexcept>(d2r::rva::ClientSkillTargetUnit)(unit);
	int32_t tx = 0, ty = 0;
	if (target == nullptr || !d2r::UnitXY(target, &tx, &ty)) {
		++g_fireClawClients;
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, d2r::rva::SkCltMissileA);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		return 0;
	}
	const int32_t count =
		d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, d2r::rva::SkCalc2), skillId, level);
	const int32_t created = Walls(unit, missile, count, skillId, level, tx, ty, [](d2r::MissileParams* p) {
		return d2r::api.clientCreateMissile(p, 0) != nullptr;
	});
	++g_fireClawClients;
	return 1;
}

// --- Spirit Wolf and Fenris start (PD2 srvstfunc 77; docs/research/skills/druid-w24.md) ----------------------------------
// Read from ProjectDiablo.dll: srvst 77 at 0x103b6c80, its rule 0x103747b0. A player only; pet types 11 and 12 only.
// Once the type has a stored max: while the player has wolves of the type and their count is not under petmax, the
// first one dies; with none left, petmax 0 refuses. D2R's do (srvdo 119) summons at least one even at petmax 0.

}  // namespace

PD2R_SLOT(41, SrvDo, 175, static_cast<SrvDoFn>(SrvDoFireClaws), "fire claws")
PD2R_SLOT(42, CltDo, 110, static_cast<CltDoFn>(CltDoFireClaws), "fire claws")
