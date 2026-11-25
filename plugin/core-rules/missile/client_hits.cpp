// Client hits around the hit point (W17) and the assassin charge-up finishers' client missiles (W30).
// Moved verbatim from missile_functions.cpp (#2198).
#include "shared.h"

namespace missiles {

std::atomic<uint32_t> g_holyLightHits {0};
std::atomic<uint32_t> g_fistDelayHits {0};
std::atomic<uint32_t> g_chargeExplodes {0};
std::atomic<uint32_t> g_chargeBoltHits {0};

auto CltMissileRecord(Unit* missile) noexcept -> const uint8_t* {
	return d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::rva::MissileRecord)(
		d2r::api.unitDataFlag(missile), At<int32_t>(missile, d2r::rva::UnitClassId));
}

}  // namespace missiles

// --- Client hits around the hit point (W17, docs/research/skills/w17-holy-light.md) ---------------------------------------
namespace {


using ClientAreaCallback = int32_t (*)(d2r::ClientAreaContext*, Unit*) noexcept;
using ClientAreaFn = void (*)(Unit*, int32_t, int32_t, int32_t, uint32_t, ClientAreaCallback, void*, int32_t) noexcept;

struct ClientBurst {
	Unit* owner;
	Unit* missile;
	Unit* skip;  // a unit that gets no sub-missile
	int32_t sub, skillId, level, key;
	int32_t created;
};


// PD2's callback 0x103a6bd0: CltHitSubMissile1 placed on each unit in range but the one hit (flags 0x22, the unit as
// origin and target).
auto HolyLightClientOne(d2r::ClientAreaContext* area, Unit* unit) noexcept -> int32_t {
	auto* b = static_cast<ClientBurst*>(area->arg);
	if (unit == b->skip) {
		return 0;
	}
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x22;
	params.owner = b->owner;
	params.origin = unit;
	params.target = unit;
	params.missile = b->sub;
	params.skill = b->skillId;
	params.skillLevel = b->level;
	b->created += d2r::api.clientCreateMissile(&params, 0) != nullptr ? 1 : 0;
	return 1;
}

// 1.13c's callback 0x6fb663c0 (D2R's 0x1bc380 without its swap): CltHitSubMissile1 from the delay missile to each unit
// in range (flags 0x20), the struck unit included.
auto FistBoltOne(d2r::ClientAreaContext* area, Unit* unit) noexcept -> int32_t {
	auto* b = static_cast<ClientBurst*>(area->arg);
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x20;
	params.owner = b->owner;
	params.origin = b->missile;
	params.missile = b->sub;
	if (!d2r::UnitXY(unit, &params.targetX, &params.targetY)) {
		return 1;
	}
	params.skill = b->skillId;
	params.skillLevel = b->level;
	params.sourceKey = b->key;
	b->created += d2r::api.clientCreateMissile(&params, 0) != nullptr ? 1 : 0;
	return 1;
}

}  // namespace

// chit 66 (PD2 0x103a69c0): the hit's CltHitSubMissile1 on every unit within cHitPar1 of the missile but the unit hit,
// with the row's Skill. PD2's holylightdelay names no CltHitSubMissile1, so its hit draws nothing more.
auto CltHitHolyLight(Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t* row = missile != nullptr ? CltMissileRecord(missile) : nullptr;
	if (row == nullptr) {
		return 1;
	}
	Unit* owner = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientMissileOwner)(missile);
	ClientBurst b {owner, missile, hit, At<int16_t>(row, MsCltHitSubMissile1), At<int16_t>(row, MsSkill),
	               d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile), 0, 0};
	int32_t x = 0, y = 0;
	const int32_t radius = At<int32_t>(row, MsCltHitPar1);
	const bool burst = owner != nullptr && d2r::ValidMissile(d2r::api.unitDataFlag(missile), b.sub) &&
	                   d2r::UnitXY(missile, &x, &y);
	if (burst) {
		d2r::Engine<ClientAreaFn>(ClientAreaUnits)(owner, x, y, radius, 0, HolyLightClientOne, &b, 0);
	}
	++g_holyLightHits;
	return 1;
}

// chit 71 (PD2 26, vanilla 1.13c D2Client 0xbad40): CltHitSubMissile1 from the delay missile to every unit within
// cHitPar1 (else the skill's aurarangecalc, at least 1). Zero when the missile's target unit is gone, as both do.
// D2R's 26 (0x1adcb0) differs: it stops at cHitPar2 / calc4 bolts and aims the struck unit's bolt at the caster.
// shortcut: no bolt limit; 1.13c computes cHitPar2 / calc4 and never stores it, so its callback compares against an unset
// stack slot. Upgrade if PD2 in play shows a bolt count cap (docs/deviations.md).
auto CltHitFistDelay(Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	(void)hit;
	const uint8_t* row = missile != nullptr ? CltMissileRecord(missile) : nullptr;
	if (row == nullptr || At<int16_t>(row, MsHitSubMissile1) < 0) {
		return 1;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(missile);
	Unit* owner = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientMissileOwner)(missile);
	const int32_t skillId = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileSkill)(missile);
	const int32_t level = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile);
	const uint8_t* skill = owner != nullptr ? d2r::api.getSkillRecord(dataFlag, skillId) : nullptr;
	if (skill == nullptr) {
		return 1;
	}
	const auto id = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileTargetId)(missile);
	const auto type = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileTargetType)(missile);
	if (d2r::Engine<Unit* (*)(int32_t, int32_t) noexcept>(ClientFindUnit)(id, type) == nullptr) {
		return 0;
	}
	int32_t radius = At<int32_t>(row, MsCltHitPar1);
	if (radius < 1) {
		radius = std::max(d2r::api.evalSkillCalc(dataFlag, owner, At<int32_t>(skill, SkAuraRangeCalc), skillId, level), 1);
	}
	ClientBurst b {owner, missile, nullptr, At<int16_t>(row, MsCltHitSubMissile1), skillId, level,
	               d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(missile), 0};
	int32_t x = 0, y = 0;
	const bool burst = d2r::ValidMissile(dataFlag, b.sub) && d2r::UnitXY(missile, &x, &y);
	if (burst) {
		d2r::Engine<ClientAreaFn>(ClientAreaUnits)(owner, x, y, radius, At<int32_t>(skill, SkAuraFilter) != 0 ? 0xa683 : 0,
		                                           FistBoltOne, &b, 0);
	}
	++g_fistDelayHits;
	return 1;
}

// --- Assassin charge-up finishers' client missiles (W30, docs/research/skills/w30-assassin-finishers.md) ------------------
namespace {


}  // namespace

// cdo 79 (PD2 cdo 69, 0x103a60d0; fistsoffireexplode, bladesoficecubes): the missile's step (PD2 calls 1.13c's
// D2Client 0xb17c0 first, D2R's cltdo 1), then on its first frame CltSubMissile1 (fistsoffirefirewall,
// bladesoficeexplode) placed at its position (flags 3, no aim), with its skill and level; none without an owner,
// as in PD2.
void CltDoChargeExplode(Unit* missile, float partFrame) noexcept {
	using namespace d2r::rva;
	if (missile == nullptr) {
		return;
	}
	d2r::Engine<MissileCltDoFn>(MissileCltDoDefault)(missile, partFrame);
	if (d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileCltFrames)(missile) != 1) {
		return;
	}
	const uint8_t* row = CltMissileRecord(missile);
	Unit* owner = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientMissileOwner)(missile);
	const int32_t sub = row != nullptr ? At<int16_t>(row, MsCltSubMissile1) : -1;
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	if (owner == nullptr || !d2r::ValidMissile(d2r::api.unitDataFlag(missile), sub) ||
	    !d2r::UnitXY(missile, &params.x, &params.y)) {
		return;
	}
	params.flags = MissileAtXY | MissileAimOffset;
	params.owner = owner;
	params.missile = sub;
	params.skill = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileSkill)(missile);
	params.skillLevel = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile);
	const bool created = d2r::api.clientCreateMissile(&params, 0) != nullptr;
	++g_chargeExplodes;
}

// chit 67 (PD2 0x103a6aa0; clawsofthunderbolt): CltHitSubMissile1 (cotthunderstorm) placed at the unit hit (flags 1)
// with the row's Skill and the missile's level, raised CHitCalc1 (its z height) and moving cHitPar1-3 per frame, as
// PD2 writes them through 1.13c's D2Client 0xc7e30 and the graphic's motion fields (D2R: SetMissileOffset/Motion).
auto CltHitChargeBolt(Unit* missile, Unit* hit) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t* row = missile != nullptr ? CltMissileRecord(missile) : nullptr;
	if (row == nullptr || hit == nullptr) {
		return 1;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(missile);
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = MissileAtXY;
	params.owner = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientMissileOwner)(missile);
	params.missile = At<int16_t>(row, MsCltHitSubMissile1);
	params.skill = At<int16_t>(row, MsSkill);
	params.skillLevel = d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileLevel)(missile);
	if (!d2r::ValidMissile(dataFlag, params.missile) || !d2r::UnitXY(hit, &params.x, &params.y)) {
		return 1;
	}
	Unit* bolt = d2r::api.clientCreateMissile(&params, 0);
	int32_t z = 0;
	if (bolt != nullptr) {
		using EvalFn = int32_t (*)(uint32_t, Unit*, Unit*, int32_t, int32_t, int32_t) noexcept;
		// shortcut: no owner gives height 0 where PD2 still evaluates CHitCalc1 (D2R's evaluator is only known to run
		// with one); PD2's only user has the constant 280. Upgrade if a row's CHitCalc1 reads its owner.
		z = params.owner != nullptr ? d2r::Engine<EvalFn>(EvalMissileCalc)(dataFlag, missile, params.owner,
		                                                                  At<int32_t>(row, MsCltHitCalc1), -1, -1)
		                            : 0;
		d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetMissileOffset)(bolt, 0, 0, z);
		d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t, int32_t) noexcept>(SetMissileMotion)(
			bolt, At<int32_t>(row, MsCltHitPar1), At<int32_t>(row, MsCltHitPar2), At<int32_t>(row, MsCltHitPar3), 0);
	}
	++g_chargeBoltHits;
	return 1;
}
