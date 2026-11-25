// poison_dagger
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

// --- Poison Dagger (PD2 srvdofunc 164 -> router 190, cltdofunc 102 -> 122; docs/research/skills/necromancer-w11.md) ---------
// PD2's srvstfunc 68 only calls slot 32, D2R's melee start, so the merge points it there (pd2_port.FUNCS).
std::atomic<uint32_t> g_poisonDaggers {0};

std::atomic<uint32_t> g_poisonDaggerClients {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t PoisonDagger = 73, PoisonDaggerSpiderBoss = 550;

constexpr int32_t ClientRingCount = 6;  // PD2's cltdo 102 passes 6, whatever the skill's Param3


// The skill's rolled poison as D2R's fill makes it (poison +0x38 = min + rand(max - min), length ELen).

}  // namespace

namespace pd2r::skills {

// The skill's rolled poison as D2R's fill makes it (poison +0x38 = min + rand(max - min), length ELen).
auto SkillPoison(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	uint8_t damage[0x180] {};  // the damage block size Leap's knockback uses (LeapKnockback)
	d2r::Engine<void (*)(Unit*, uint8_t*, int32_t, int32_t) noexcept>(d2r::rva::ElementalDamage)(unit, damage, skillId,
	                                                                                             level);
	return At<int32_t>(damage, d2r::rva::DmgPoison);
}

// PD2 adds a skill's poison to a missile's own (Poison Dagger's clouds, Cobra Strike's charge missiles).

// PD2 adds a skill's poison to a missile's own (Poison Dagger's clouds, Cobra Strike's charge missiles).
void AddMissilePoison(Unit* missile, int32_t poison) noexcept {
	using namespace d2r::rva;
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const auto setStat = d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat);
	setStat(missile, StatPoisonMin, unitStat(missile, StatPoisonMin, 0) + poison, 0);
	setStat(missile, StatPoisonMax, unitStat(missile, StatPoisonMax, 0) + poison, 0);
}

// PD2 srvdo 164 (ProjectDiablo.dll 0x103b25c0): the melee hit (D2R's own Poison Dagger do, slot 32, whose start
// already put the skill's poison in the queued hit), then a ring of Param3 srvmissilea clouds around the target, each
// with the rolled poison added to its poisonmindam and poisonmaxdam.
// shortcut: PD2 scales the poison by its rolled length over the skill's ELen; the fill sets the length to ELen, so the
// factor is 1 here. Upgrade if a length bonus lands between the two.

}  // namespace pd2r::skills

namespace {

// PD2 srvdo 164 (ProjectDiablo.dll 0x103b25c0): the melee hit (D2R's own Poison Dagger do, slot 32, whose start
// already put the skill's poison in the queued hit), then a ring of Param3 srvmissilea clouds around the target, each
// with the rolled poison added to its poisonmindam and poisonmaxdam.
// shortcut: PD2 scales the poison by its rolled length over the skill's ELen; the fill sets the length to ELen, so the
// factor is 1 here. Upgrade if a length bonus lands between the two.
auto SrvDoPoisonDagger(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	int32_t tx = 0, ty = 0;
	const bool aimed = skill != nullptr && target != nullptr && d2r::UnitXY(target, &tx, &ty);  // before the hit kills it
	const int64_t ran = VanillaSlot<SrvDoFn>(SrvDoTable, d2r::rva::SrvDoPoisonDagger)(game, unit, skillId, level);
	if (!aimed) {
		return ran;
	}
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		if (static std::atomic<bool> logged {false}; !logged.exchange(true)) {  // bad table data repeats every cast
			plog::Warn("poison dagger: skill %d has no valid srvmissilea (row %d)", skillId, missile);
		}
		return ran;
	}
	const int32_t poison = SkillPoison(unit, skillId, level);

	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, missile, tx, ty, skillId, level);
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const int32_t count = At<int32_t>(skill, SkParam3);
	int32_t cloudPoison = 0, cloudLength = 0;  // as a cloud carries them, for the log
	const int32_t created = Ring(count, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		Unit* cloud = d2r::api.createMissile(game, &params);
		if (cloud != nullptr) {
			AddMissilePoison(cloud, poison);
			cloudPoison = unitStat(cloud, StatPoisonMin, 0);
			cloudLength = unitStat(cloud, StatPoisonLength, 0);
		}
		return cloud != nullptr;
	});
	++g_poisonDaggers;
	return 1;
}

// PD2 cltdo 102 (0x103ab760): the clouds' client copies, cltmissilea in a ring of ClientRingCount around the target.

// PD2 cltdo 102 (0x103ab760): the clouds' client copies, cltmissilea in a ring of ClientRingCount around the target.
auto CltDoPoisonDagger(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	Unit* target = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientSkillTargetUnit)(unit);
	int32_t tx = 0, ty = 0;
	if (skill == nullptr || target == nullptr || !d2r::UnitXY(target, &tx, &ty)) {
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, SkCltMissileA);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		return 0;
	}
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, missile, tx, ty, skillId, level);
	const int32_t created = Ring(ClientRingCount, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		return d2r::api.clientCreateMissile(&params, 0) != nullptr;
	});
	++g_poisonDaggerClients;
	return 1;
}

// --- Gust, Hunger (PD2 srvdofunc 156 and 165 -> router 190; docs/research/skills/druid-w23.md) --------------------------

}  // namespace

PD2R_SLOT(24, CltDo, 122, static_cast<CltDoFn>(CltDoPoisonDagger), "poison dagger")
PD2R_ROUTE(2, PoisonDagger, SrvDoPoisonDagger)
PD2R_ROUTE(3, PoisonDaggerSpiderBoss, SrvDoPoisonDagger)
