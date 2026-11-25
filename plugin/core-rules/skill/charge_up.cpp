// charge_up
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

constexpr int32_t ProgressiveSteal = 319;  // aurastat1 of Cobra Strike and RakanothStrike (the same id in PD2)

constexpr int32_t StatLifeDrain = 60, StatManaDrain = 62;  // lifedrainmindam, manadrainmindam: D2R and PD2

}  // namespace

namespace pd2r::skills {

std::atomic<uint32_t> g_chargeHits {0};


}  // namespace pd2r::skills

namespace {

auto ListStat(uint32_t dataFlag, void* list, int32_t stat) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(d2r::rva::ListStatValue)(dataFlag, list,
	                                                                                                   stat, 0);
}

// The skill's charges: aurastat1 on its aurastate list (PD2's 0x1037ddf0), 0 with none.

// The skill's charges: aurastat1 on its aurastate list (PD2's 0x1037ddf0), 0 with none.
auto Charges(uint32_t dataFlag, Unit* unit, const uint8_t* skill) noexcept -> int32_t {
	const int32_t state = At<int16_t>(skill, d2r::rva::SkAuraState), stat = At<int16_t>(skill, d2r::rva::SkAuraStat1);
	void* list = state >= 0 && stat >= 0
	                 ? d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(d2r::rva::UnitStateList)(unit, state)
	                 : nullptr;
	return list != nullptr ? ListStat(dataFlag, list, stat) : 0;
}

// PD2's shared charges (0x1036ee80): the highest charge count over the unit's progressive states, each read as the
// charges of the skill that made the list (stat 350), clamped to 0-3. With `clear`, every progressive list is
// dropped as well (PD2 does that when the count is 3).

// PD2's shared charges (0x1036ee80): the highest charge count over the unit's progressive states, each read as the
// charges of the skill that made the list (stat 350), clamped to 0-3. With `clear`, every progressive list is
// dropped as well (PD2 does that when the count is 3).
auto SharedCharges(uint32_t dataFlag, Unit* unit, bool clear) noexcept -> int32_t {
	using namespace d2r::rva;
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	if (tables == nullptr) {
		return 0;
	}
	const auto* states = At<const int16_t*>(tables, TablesProgressiveStates);
	const int64_t count = At<int64_t>(tables, TablesProgressiveCount);
	int32_t most = 0;
	for (int64_t i = 0; states != nullptr && i < count; ++i) {
		void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, states[i]);
		if (list == nullptr) {
			continue;
		}
		if (clear) {
			DropStateList(dataFlag, unit, states[i]);
			continue;
		}
		const uint8_t* maker = d2r::api.getSkillRecord(dataFlag, ListStat(dataFlag, list, StatListSkill));
		if (maker != nullptr && At<int16_t>(maker, SkAuraStat1) >= 0) {
			most = std::max(most, ListStat(dataFlag, list, At<int16_t>(maker, SkAuraStat1)));
		}
	}
	return std::clamp(most, 0, 3);
}

// Cobra Strike's steal (PD2 srvdo 170 at its 2nd charge): a 3-frame stat list with lifedrainmindam and
// manadrainmindam = calc1 and damagepercent = calc2. PD2 builds it with list flags 4 and no unit event.

// Cobra Strike's steal (PD2 srvdo 170 at its 2nd charge): a 3-frame stat list with lifedrainmindam and
// manadrainmindam = calc1 and damagepercent = calc2. PD2 builds it with list flags 4 and no unit event.
void AddStealList(Game* game, Unit* unit, const uint8_t* skill, int32_t skillId, int32_t level) noexcept {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const int32_t id = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitId)(unit, __FILE__, __LINE__);
	void* list = d2r::Engine<void* (*)(int32_t, int32_t, int32_t, int32_t) noexcept>(NewStatList)(
		4, At<int32_t>(game, GameFrame) + 3, At<int32_t>(unit, UnitType), id);
	if (list == nullptr) {
		return;
	}
	const auto setStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	const int32_t steal = d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level);
	setStat(dataFlag, list, StatLifeDrain, steal, 0);
	setStat(dataFlag, list, StatManaDrain, steal, 0);
	setStat(dataFlag, list, StatDamagePercent, d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level), 0);
	d2r::Engine<void (*)(Unit*, void*, int32_t) noexcept>(AttachStatList)(unit, list, 1);
}

// PD2 srvdo 170 (ProjectDiablo.dll 0x103b2c60): a charge-up hit. Shared charges: a count of 3 on any progressive
// state clears them all first; a hit then tops this skill up to the highest count + 1 (at most 3), fires
// srvprgfunc[charges] at once and sends the charge packet. The hit itself is D2R's charge-up do function (srvdo 34,
// whose AddCharge on a hit is the first charge); PD2 calls no hand-alternating 35.
// shortcut: the hit is D2R's (to-hit without PD2's added progressive_tohit, a hit counted on the queued result
// where PD2 skips results 0x8390 on monsters and takes back the charges if the applied hit misses), the prg function
// fires after the hit is applied (PD2: just before), and PD2's in-build progressive bonuses (vanilla 0x922a0 /
// 0x91e30, which D2R only runs inside its release) are not run.
// Upgrade with a port of srvdo 34's hit if play shows the charge counts or damage off PD2's (docs/deviations.md).

// PD2 srvdo 170 (ProjectDiablo.dll 0x103b2c60): a charge-up hit. Shared charges: a count of 3 on any progressive
// state clears them all first; a hit then tops this skill up to the highest count + 1 (at most 3), fires
// srvprgfunc[charges] at once and sends the charge packet. The hit itself is D2R's charge-up do function (srvdo 34,
// whose AddCharge on a hit is the first charge); PD2 calls no hand-alternating 35.
// shortcut: the hit is D2R's (to-hit without PD2's added progressive_tohit, a hit counted on the queued result
// where PD2 skips results 0x8390 on monsters and takes back the charges if the applied hit misses), the prg function
// fires after the hit is applied (PD2: just before), and PD2's in-build progressive bonuses (vanilla 0x922a0 /
// 0x91e30, which D2R only runs inside its release) are not run.
// Upgrade with a port of srvdo 34's hit if play shows the charge counts or damage off PD2's (docs/deviations.md).
auto SrvDoChargeUp(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	int32_t shared = SharedCharges(dataFlag, unit, false);
	if (shared == 3) {
		SharedCharges(dataFlag, unit, true);
		shared = 0;
	}
	const int32_t before = Charges(dataFlag, unit, skill);
	const int32_t state = At<int16_t>(skill, SkAuraState), stat = At<int16_t>(skill, SkAuraStat1);
	// PD2 adds the steal at the 2nd charge, before the hit's damage, so its leech counts on that hit. The hit isn't
	// known yet here: the list goes on whenever a hit would pass 2, and a miss keeps it its 3 frames.
	const bool steal = stat == ProgressiveSteal && before <= 1 && shared >= 1;
	if (steal) {
		AddStealList(game, unit, skill, skillId, level);
	}
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	const int64_t ran = VanillaSlot<SrvDoFn>(SrvDoTable, d2r::rva::SrvDoChargeUp)(game, unit, skillId, level);
	int32_t charges = Charges(dataFlag, unit, skill);
	int32_t prg = 0, prgCalc = 0;
	if (charges > before && state >= 0 && stat >= 0) {
		g_chargeHits.fetch_add(1);
		const auto addCharge = d2r::Engine<int64_t (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t) noexcept>(AddCharge);
		while (charges <= shared && charges < 3) {
			addCharge(game, unit, skillId, level, state, stat);
			const int32_t now = Charges(dataFlag, unit, skill);
			if (now <= charges) {
				break;  // AddCharge made no list: stop rather than spin
			}
			charges = now;
		}
		const int32_t n = std::min(charges, 3);
		prg = At<int16_t>(skill, SkSrvPrgFunc1 + (n - 1) * 2);
		prgCalc = d2r::EvalCalc(game, unit, skill, SkPrgCalc1 + (n - 1) * 4, skillId, level);
		const SrvDoFn fn = prg > 0 && prg < SrvDoSlots ? VanillaSlot<SrvDoFn>(SrvDoTable, prg) : nullptr;
		if (fn != nullptr) {
			fn(game, unit, skillId, level);
		} else {
			prg = 0;
		}
		// the packet's seed is the unit's next random number, the seed itself left as it was (PD2 and the release)
		const uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
		uint32_t next[2] = {seed[0], seed[1]};
		d2r::RandomBelow(next, 1);
		using PacketFn = void (*)(Unit*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept;
		d2r::Engine<PacketFn>(SendChargePacket)(unit, target, skillId, level, static_cast<int32_t>(next[0]), 0, n);
	}
	return ran;
}

// The prg functions' charge (PD2 reads it with 0x1037ddf0 and runs only at 1-3) and the missile it picks.

// The prg functions' charge (PD2 reads it with 0x1037ddf0 and runs only at 1-3) and the missile it picks.
struct ChargeMissile {
	const uint8_t* skill = nullptr;  // null: don't run
	int32_t charge = 0, missile = -1, count = 0;
};


auto ChargeMissileOf(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> ChargeMissile {
	const uint32_t dataFlag = d2r::DataFlag(game);
	ChargeMissile m;
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return m;
	}
	m.charge = Charges(dataFlag, unit, skill);
	m.missile = d2r::api.pickSkillMissile(unit, skillId);
	if (m.charge < 1 || m.charge > 3 || !d2r::ValidMissile(dataFlag, m.missile)) {
		return m;
	}
	m.count = d2r::EvalCalc(game, unit, skill, d2r::rva::SkPrgCalc1 + (m.charge - 1) * 4, skillId, level);
	m.skill = skill;
	return m;
}

// PD2's charge spread (0x1037d780): Multiple Shot's line through the target, stepping the other way round, with
// PD2's own handling of a target within 2 subtiles.

constexpr auto ChargeSpread(int32_t ux, int32_t uy, int32_t tx, int32_t ty, int32_t count) noexcept -> Spread {
	int32_t dx = ux - tx, dy = uy - ty, d2 = dx * dx + dy * dy;
	if (d2 < 4) {
		dx *= 4;
		dy = d2;  // sic: PD2 keeps the old length here, not dy * 4
		d2 = d2 * d2 + dx * dx;
	}
	int32_t sx = d2 >= 16 ? dy : 2 * dy, sy = -(d2 >= 16 ? dx : 2 * dx);
	while (sx * sx + sy * sy > 3) {
		sx /= 2;
		sy /= 2;
	}
	return {tx - sx * count / 2, ty - sy * count / 2, sx, sy};
}

static_assert(ChargeSpread(0, 0, 10, 0, 3).x == 10 && ChargeSpread(0, 0, 10, 0, 3).y == -1);  // 3 across the aim

static_assert(ChargeSpread(0, 0, 10, 0, 3).dx == 0 && ChargeSpread(0, 0, 10, 0, 3).dy == 1);

// PD2 srvprg 171 (0x103b30a0), here 184: prgcalc[charge] missiles of the charge's missile from the caster to points
// along ChargeSpread; Cobra Strike's carry the skill's poison (PD2 scales it by its rolled length over ELen, which is
// 1 here as in Poison Dagger).

// PD2 srvprg 171 (0x103b30a0), here 184: prgcalc[charge] missiles of the charge's missile from the caster to points
// along ChargeSpread; Cobra Strike's carry the skill's poison (PD2 scales it by its rolled length over ELen, which is
// 1 here as in Poison Dagger).
auto SrvPrgSpread(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const ChargeMissile m = ChargeMissileOf(game, unit, skillId, level);
	int32_t ux = 0, uy = 0, tx = 0, ty = 0;
	if (m.skill == nullptr || !d2r::UnitXY(unit, &ux, &uy) || d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0) {
		return 0;
	}
	const int32_t poison = skillId == CobraStrike ? SkillPoison(unit, skillId, level) : 0;
	const int32_t count = ClampCount(m.count);
	d2r::MissileParams params =
		SkillMissileParams(unit, MissileFromOrigin | MissileFlag10000, m.missile, ux, uy, skillId, level);  // PD2's 0x10820
	params.activation = 1;  // PD2 sets 1.13c's +0x44 to 1
	Spread s = ChargeSpread(ux, uy, tx, ty, count);
	int32_t created = 0;
	for (int32_t i = 0; i < count; ++i, s.x += s.dx, s.y += s.dy) {
		params.targetX = s.x;
		params.targetY = s.y;
		Unit* missile = d2r::api.createMissile(game, &params);
		if (missile != nullptr) {
			++created;
			if (poison != 0) {
				AddMissilePoison(missile, poison);
			}
		}
	}
	return 1;
}

// PD2 srvprg 172 (0x103b3360), here 185: a ring of prgcalc[charge] (Ring's count) of the charge's missile at the
// target, or at the caster with none; Cobra Strike's carry the skill's poison as in the spread.

// PD2 srvprg 172 (0x103b3360), here 185: a ring of prgcalc[charge] (Ring's count) of the charge's missile at the
// target, or at the caster with none; Cobra Strike's carry the skill's poison as in the spread.
auto SrvPrgRing(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const ChargeMissile m = ChargeMissileOf(game, unit, skillId, level);
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	int32_t x = 0, y = 0;
	if (m.skill == nullptr || !d2r::UnitXY(target != nullptr ? target : unit, &x, &y)) {
		return 0;
	}
	const int32_t poison = skillId == CobraStrike ? SkillPoison(unit, skillId, level) : 0;
	d2r::MissileParams params =
		SkillMissileParams(unit, MissileAtXY | MissileAimOffset, m.missile, x, y, skillId, level);  // PD2's 3
	const int32_t created = Ring(m.count, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		Unit* missile = d2r::api.createMissile(game, &params);
		if (missile != nullptr && poison != 0) {
			AddMissilePoison(missile, poison);
		}
		return missile != nullptr;
	});
	return 1;
}

// PD2 srvprg 39 (0x103aee00), here 186: vanilla srvdo 38, then 39 (Fists of Fire's first charge runs both of
// vanilla's later fire charges); D2R's 38 and 39 are taken to match 1.13c's.

// PD2 srvprg 39 (0x103aee00), here 186: vanilla srvdo 38, then 39 (Fists of Fire's first charge runs both of
// vanilla's later fire charges); D2R's 38 and 39 are taken to match 1.13c's.
auto SrvPrgFire(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	const int32_t charge = skill != nullptr ? Charges(d2r::DataFlag(game), unit, skill) : 0;
	if (charge < 1 || charge > 3) {
		return 0;  // PD2's 0x1037ddf0 rule, as in ChargeMissileOf
	}
	const int64_t first = VanillaSlot<SrvDoFn>(d2r::rva::SrvDoTable, d2r::rva::SrvDoFireCharge1)(game, unit, skillId, level);
	const int64_t second = VanillaSlot<SrvDoFn>(d2r::rva::SrvDoTable, d2r::rva::SrvDoFireCharge2)(game, unit, skillId, level);
	return 1;
}

// PD2 srvprg 139 (0x103b0cf0), here 181: D2R's cold missile (srvdo 139, 0x585440) with the charge's missile, where
// D2R's takes srvmissilea: one missile thrown at the skill's point, with the aim offset D2R's 139 sets.

// PD2 srvprg 139 (0x103b0cf0), here 181: D2R's cold missile (srvdo 139, 0x585440) with the charge's missile, where
// D2R's takes srvmissilea: one missile thrown at the skill's point, with the aim offset D2R's 139 sets.
auto SrvPrgCold(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	const int32_t missile = d2r::api.pickSkillMissile(unit, skillId);
	const int32_t charge = skill != nullptr ? Charges(dataFlag, unit, skill) : 0;
	if (charge < 1 || charge > 3 || missile <= 0 || !d2r::ValidMissile(dataFlag, missile)) {
		return 0;
	}
	int32_t tx = 0, ty = 0, ux = 0, uy = 0;
	if (d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0 || !d2r::UnitXY(unit, &ux, &uy)) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	using ThrowFn = Unit* (*)(Game*, int32_t, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
	                          int32_t) noexcept;
	const int32_t key = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(unit);
	Unit* thrown = d2r::Engine<ThrowFn>(ThrowSkillMissile)(game, missile, unit, skillId, level, key, 0, 0, 0, 0, 0);
	if (thrown == nullptr) {
		return 0;
	}
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileBounces)(thrown, uy - ty);
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileTargetDx)(thrown, tx - ux);
	return 1;
}

// --- The finishers' client prg functions (PD2 cltprg 106 -> 127, 107 -> 128; docs/research/skills/w30-assassin-finishers.md)
// D2R's charge/release packet handler (0x216380) sets the list's charge before each cltprgfunc call, as 1.13c's does.

// The charge, the client missile PD2's 1.13c pick (D2Client 0x40550) gives it (cltmissilea/b/c for 1/2/3) and
// cltcalc[charge]; PD2's client prg functions run only at 1-3, as the server ones do.

// The charge, the client missile PD2's 1.13c pick (D2Client 0x40550) gives it (cltmissilea/b/c for 1/2/3) and
// cltcalc[charge]; PD2's client prg functions run only at 1-3, as the server ones do.
auto ClientChargeMissileOf(Unit* unit, int32_t skillId, int32_t level) noexcept -> ChargeMissile {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	ChargeMissile m;
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	m.charge = skill != nullptr ? Charges(dataFlag, unit, skill) : 0;
	if (m.charge < 1 || m.charge > 3) {
		return m;
	}
	m.missile = At<int16_t>(skill, SkCltMissileA + (m.charge - 1) * 2);
	if (!d2r::ValidMissile(dataFlag, m.missile)) {
		return m;
	}
	m.count = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, SkCltCalc1 + (m.charge - 1) * 4), skillId,
	                                 level);
	m.skill = skill;
	return m;
}

// PD2 cltprg 106 (0x103ab920), here 127: the client half of srvprg 184, cltcalc[charge] of the charge's client
// missile from the caster along ChargeSpread to the skill's target unit (else its target point).

// PD2 cltprg 106 (0x103ab920), here 127: the client half of srvprg 184, cltcalc[charge] of the charge's client
// missile from the caster along ChargeSpread to the skill's target unit (else its target point).
auto CltPrgSpread(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (unit == nullptr) {
		return 0;
	}
	const ChargeMissile m = ClientChargeMissileOf(unit, skillId, level);
	Unit* target = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientSkillTargetUnit)(unit);
	int32_t ux = 0, uy = 0, tx = 0, ty = 0;
	const bool aimed = target != nullptr ? d2r::UnitXY(target, &tx, &ty)
	                                     : d2r::api.clientSkillTargetXY(unit, &tx, &ty) != 0;
	if (m.skill == nullptr || !aimed || !d2r::UnitXY(unit, &ux, &uy)) {
		return 0;
	}
	const int32_t count = ClampCount(m.count);  // PD2's do-while makes one at 0 or less
	d2r::MissileParams params =
		SkillMissileParams(unit, MissileFromOrigin | MissileFlag10000, m.missile, ux, uy, skillId, level);
	params.activation = 1;
	Spread s = ChargeSpread(ux, uy, tx, ty, count);
	int32_t created = 0;
	for (int32_t i = 0; i < count; ++i, s.x += s.dx, s.y += s.dy) {
		params.targetX = s.x;
		params.targetY = s.y;
		created += d2r::api.clientCreateMissile(&params, 0) != nullptr ? 1 : 0;
	}
	return 1;
}

// PD2 cltprg 107 (0x103aba80), here 128: the client half of srvprg 185, PD2's client ring (0x1036d370) of
// cltcalc[charge] of the charge's client missile at the skill's target unit, or at the caster with none.

// PD2 cltprg 107 (0x103aba80), here 128: the client half of srvprg 185, PD2's client ring (0x1036d370) of
// cltcalc[charge] of the charge's client missile at the skill's target unit, or at the caster with none.
auto CltPrgRing(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (unit == nullptr) {
		return 0;
	}
	const ChargeMissile m = ClientChargeMissileOf(unit, skillId, level);
	Unit* target = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientSkillTargetUnit)(unit);
	int32_t x = 0, y = 0;
	if (m.skill == nullptr || !d2r::UnitXY(target != nullptr ? target : unit, &x, &y)) {
		return 0;
	}
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, m.missile, x, y, skillId, level);
	const int32_t created = Ring(m.count, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		return d2r::api.clientCreateMissile(&params, 0) != nullptr;
	});
	return 1;
}

// --- Firestorm and the wall makers (PD2 srvdofunc 166 -> router 190, cltdofunc 119) ------------------------------
// Read from ProjectDiablo.dll: srvdo 166 at 0x103b29d0, cltdo 119 at 0x103acb70, their maker callback 0x1036f220
// (docs/research/skills/druid-w22.md). calc1 makers of srvmissilea (the client reads it too), all from the caster to the
// target, each with the maker callback; D2R's Firestorm (srvdo 117) sends its first maker without it and DiabWall's
// (srvdo 102) skips the path one time in five.

}  // namespace

PD2R_SLOT(32, SrvDo, 184, static_cast<SrvDoFn>(SrvPrgSpread), "charge spread")
PD2R_SLOT(33, SrvDo, 185, static_cast<SrvDoFn>(SrvPrgRing), "charge ring")
PD2R_SLOT(34, SrvDo, 186, static_cast<SrvDoFn>(SrvPrgFire), "charge fire")
PD2R_SLOT(35, SrvDo, 181, static_cast<SrvDoFn>(SrvPrgCold), "charge cold")
PD2R_SLOT(44, CltDo, 127, static_cast<CltDoFn>(CltPrgSpread), "charge spread")
PD2R_SLOT(45, CltDo, 128, static_cast<CltDoFn>(CltPrgRing), "charge ring")
PD2R_ROUTE(34, TigerStrike, SrvDoChargeUp)
PD2R_ROUTE(35, FistsOfFire, SrvDoChargeUp)
PD2R_ROUTE(36, CobraStrike, SrvDoChargeUp)
PD2R_ROUTE(37, ClawsOfThunder, SrvDoChargeUp)
PD2R_ROUTE(38, BladesOfIce, SrvDoChargeUp)
PD2R_ROUTE(39, RoyalStrike, SrvDoChargeUp)
PD2R_ROUTE(40, RakanothStrike, SrvDoChargeUp)
