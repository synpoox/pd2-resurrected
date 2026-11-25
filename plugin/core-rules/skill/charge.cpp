// charge
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

// --- Charge (PD2 srvstfunc 72, srvdofunc 180; cltstfunc 57 -> 25, cltdofunc 112 -> 37) -------------------------------
// Read from ProjectDiablo.dll: srvst 72 at 0x103b67e0, srvdo 180 at 0x103b4870, cltst 57 at 0x103ace50, cltdo 112 at
// 0x103abfb0 (docs/research/skills/w16-charge.md). PD2's charge is vanilla's with two changes on the server: the start's speed
// adds Param1, and the hit puts calc'd splash on the caster from how far it charged. D2R's client halves (cltst 25,
// cltdo 37) already add Param1, so the merge points the client columns there.
std::atomic<uint32_t> g_charges {0};

std::atomic<uint32_t> g_chargeHitFrames {0};

// PD2's charge speed is base * (max(velocitypercent, 50) + Param1) / 100, base the class's (a monster's Run).
// D2R's server start adds calc2 instead, which PD2's row leaves empty, so its server charged slower than its client.

}  // namespace pd2r::skills

namespace {

// PD2's charge speed is base * (max(velocitypercent, 50) + Param1) / 100, base the class's (a monster's Run).
// D2R's server start adds calc2 instead, which PD2's row leaves empty, so its server charged slower than its client.
auto ChargeBase(uint32_t dataFlag, Unit* unit) noexcept -> int32_t {
	using namespace d2r::rva;
	int32_t base = 0x100;
	if (At<int32_t>(unit, UnitType) == 0) {
		const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
		const uint32_t cls = At<uint32_t>(unit, UnitClassId);
		if (tables != nullptr && cls < At<uint64_t>(tables, TablesCharStatsCount)) {
			base = At<uint8_t>(At<const uint8_t*>(tables, TablesCharStats) + cls * CharStatsSize, CsChargeVelocity) << 8;
		}
	} else {
		const uint8_t* mon = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
			dataFlag, At<int32_t>(unit, UnitClassId));
		base = mon != nullptr ? At<int16_t>(mon, MonRun) << 8 : base;  // PD2 reads charstats at a monster's id, a wrong row; D2R's starts read Run
	}
	return base;
}


constexpr auto ChargeVelocity(int32_t base, int32_t percent, int32_t param1) noexcept -> int32_t {
	return static_cast<int32_t>(static_cast<int64_t>(base) * ((percent > 50 ? percent : 50) + param1) / 100);
}

static_assert(ChargeVelocity(6 << 8, 0, 150) == 3072);  // 50% at least, plus Param1's 150%

// D2Common's distance (1.13c ordinal 10080): the longer side plus half the shorter.

// D2Common's distance (1.13c ordinal 10080): the longer side plus half the shorter.
constexpr auto ChargeDistance(int32_t dx, int32_t dy) noexcept -> int32_t {
	dx = dx < 0 ? -dx : dx;
	dy = dy < 0 ? -dy : dy;
	return dx > dy ? (2 * dx + dy) / 2 : (2 * dy + dx) / 2;
}

static_assert(ChargeDistance(10, -4) == 12 && ChargeDistance(-3, 9) == 10);

// PD2's splash on the hit: inc_splash_radius = distance / 4 * 20

// PD2's splash on the hit: inc_splash_radius = distance / 4 * 20
constexpr auto ChargeSplash(int32_t distance) noexcept -> int32_t {
	return distance / 4 * 20;
}

static_assert(ChargeSplash(12) == 60 && ChargeSplash(3) == 0);


constexpr int32_t ChargeSplashFrames = 3;  // PD2's list ends 3 frames after the hit


}  // namespace

namespace pd2r::skills {

auto CurrentUnitSkill(Unit* unit) noexcept -> UnitSkill* {
	return d2r::Engine<UnitSkill* (*)(Unit*) noexcept>(d2r::rva::CurrentSkill)(unit);
}


}  // namespace pd2r::skills

namespace {

auto& ChargeParam(UnitSkill* uskill) noexcept {
	return *reinterpret_cast<int32_t*>(uskill + d2r::rva::USkillParam);
}

// Start (PD2 srvstfunc 72): D2R's srvst 31, then, when it set off a charge (flags 0x1001), PD2's speed and the
// distance to the target in the skill's param. Any other start clears the param, so a charge cut short before its hit
// leaves no distance for the next one.

// Start (PD2 srvstfunc 72): D2R's srvst 31, then, when it set off a charge (flags 0x1001), PD2's speed and the
// distance to the target in the skill's param. Any other start clears the param, so a charge cut short before its hit
// leaves no distance for the next one.
auto SrvStCharge(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const int64_t started = VanillaSlot<SrvDoFn>(SrvStTable, d2r::rva::SrvStCharge)(game, unit, skillId, level);
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	UnitSkill* uskill = CurrentUnitSkill(unit);
	if (uskill != nullptr) {
		ChargeParam(uskill) = 0;
	}
	if (started == 0 || skill == nullptr || uskill == nullptr || (Flags(uskill) & 0x1001) != 0x1001) {
		return started;  // refused, or in reach: D2R attacked at once, as PD2 does
	}
	const int32_t base = ChargeBase(dataFlag, unit);
	const int32_t percent = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(
		unit, StatVelocityPercent, 0);
	const int32_t velocity = ChargeVelocity(base, percent, At<int32_t>(skill, SkParam1));
	SetSkillVelocity(unit, uskill, velocity);
	int32_t ux = 0, uy = 0, tx = 0, ty = 0;
	ChargeParam(uskill) = d2r::UnitXY(unit, &ux, &uy) && d2r::api.skillTargetXY(game, unit, &tx, &ty) != 0
	                          ? ChargeDistance(tx - ux, ty - uy)
	                          : 0;
	++g_charges;
	return started;
}

// Whether D2R's srvdo 67 strikes on this hit frame: its target (the skill's, else the nearest, searched as it
// searches), in reach, with the caster alive.

// Whether D2R's srvdo 67 strikes on this hit frame: its target (the skill's, else the nearest, searched as it
// searches), in reach, with the caster alive.
auto ChargeStrikes(Game* game, Unit* unit, UnitSkill* uskill) noexcept -> bool {
	using namespace d2r::rva;
	const auto* target = reinterpret_cast<const int32_t*>(uskill + USkillTargetType);
	Unit* victim = nullptr;
	if (target[0] != 6) {
		victim = d2r::Engine<Unit* (*)(Game*, int32_t, int32_t) noexcept>(FindUnit)(game, target[0], target[1]);
	}
	if (victim == nullptr) {
		using NearestFn = Unit* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t, uint32_t, int32_t*) noexcept;
		victim = d2r::Engine<NearestFn>(NearestTarget)(game, unit, 0, 0, 3, 3, 0xffffffff, nullptr);
	}
	return victim != nullptr &&
	       d2r::Engine<int32_t (*)(Unit*, Unit*, int32_t, int32_t) noexcept>(UnitInReach)(
	           unit, victim, d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitReach)(unit), 0) != 0 &&
	       d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) == 0;
}

// PD2 srvdofunc 180: D2R's srvdo 67, and when its hit goes off (flag 1 clear, target in reach) the caster first gets
// inc_splash_radius from the charge's distance on a short temp_skill_buff list, unless it has one (Frenzy's), and the
// distance is spent.

// PD2 srvdofunc 180: D2R's srvdo 67, and when its hit goes off (flag 1 clear, target in reach) the caster first gets
// inc_splash_radius from the charge's distance on a short temp_skill_buff list, unless it has one (Frenzy's), and the
// distance is spent.
auto SrvDoCharge(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	UnitSkill* uskill = CurrentUnitSkill(unit);
	if (uskill != nullptr && (Flags(uskill) & 1) == 0) {
		const uint32_t dataFlag = d2r::DataFlag(game);
		const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
		const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
		if (skill != nullptr && tables != nullptr && At<uint64_t>(tables, TablesStateCount) > TempSkillBuff &&
		    ChargeStrikes(game, unit, uskill)) {
			const int32_t distance = ChargeParam(uskill);
			const int32_t splash = ChargeSplash(distance);
			const char* list = "kept";
			if (d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, TempSkillBuff) == nullptr) {
				list = TimedStatList(dataFlag, unit, unit, skillId, level, ChargeSplashFrames, TempSkillBuff,
				                     {{IncSplashRadius, splash}});
			}
			ChargeParam(uskill) = 0;
			++g_chargeHitFrames;
		}
	}
	return VanillaSlot<SrvDoFn>(SrvDoTable, d2r::rva::SrvDoCharge)(game, unit, skillId, level);
}

// Fend, Zeal and Fury splash radius (#1936). PD2's srvst 37 (ProjectDiablo.dll 0x103b6310 in PD2.gpr, +0x10 installed)
// keeps or makes the attacker's list on the row's aurastate (temp_skill_buff) and SETS its aurastats from aurastatcalc
// (1.13c D2Game +0x448e0, D2Common #10261 is a set) on every start: inc_splash_radius = 1. Its srvdo 13 hit
// (0x10303a50, installed 0x10303a60, over 1.13c's melee hit call +0x4dc46 to +0xded30) then adds 20 to that list, when
// it has one, before the hit: 3 hits run with 21, 41, 61 (+1, +2, +3 radius), and the 61 stays until the next srvst 37,
// srvst 78 or srvdo 163. D2R's srvst 37 (93847 0x5582a0) and srvdo 13 (0x5568e0) have neither, so both slots are
// wrapped: the slot's own function is kept and called (no SDK service runs a skill's start or do function).
// shortcut: PD2's list never expires; a new one here lasts SplashFrames (10 minutes, as Frenzy's), and PD2 refuses
// the start with no target before its list step, where this sets the stat anyway. Upgrade if a later Charge or
// splash must see a leftover older than 10 minutes.

}  // namespace

PD2R_SLOT(10, SrvSt, 72, static_cast<SrvDoFn>(SrvStCharge), "charge")
PD2R_SLOT(11, SrvDo, 180, static_cast<SrvDoFn>(SrvDoCharge), "charge")
