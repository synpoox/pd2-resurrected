// leap
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

// --- Leap, Leap Attack, Leap Attack Korlic (PD2 srvdofunc 153 -> 172, cltdofunc 97 -> 115) ------------------------------
// Read from ProjectDiablo.dll: srvdo 153 at 0x103b1300, cltdo 97 at 0x103ab280 (docs/features/core-rules.md). PD2's leap path is
// D2R's (path type 9 to the skill's point); PD2 sets its own speed and, on landing, hits every unit within calc3.
std::atomic<uint32_t> g_leaps {0};

std::atomic<uint32_t> g_leapClients {0};
// Each kind's first landing and client start ([0] Leap, [1] the others), and the first landing with units in range,
// are logged too, whichever came first

}  // namespace pd2r::skills

namespace {

// Each kind's first landing and client start ([0] Leap, [1] the others), and the first landing with units in range,
// are logged too, whichever came first
constexpr int32_t SkillLeap = 132;  // PD2 keys the landing on this id: Leap knocks back, every other row hits


}  // namespace

namespace pd2r::skills {

auto UnitSkillOf(Unit* unit, int32_t skillId) noexcept -> UnitSkill* {
	return d2r::Engine<UnitSkill* (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::GetUnitSkill)(unit, skillId, 0);
}


auto Flags(UnitSkill* uskill) noexcept -> uint32_t& {
	return *reinterpret_cast<uint32_t*>(uskill + d2r::rva::USkillFlags);
}

// PD2's leap speed: a monster's monstats Run << 8, a player's 0xb00 + calc2 * 0x50 (calc2 is 80 or 60 plus
// item_leap_speed / 4), never below 0xb00. D2R's LeapStart scales the run speed by calc2 % instead.
// shortcut: PD2's client half uses 0xa00 + calc2 * 0x29; both halves take the server's speed here so the client's
// path matches the server's. Upgrade if PD2's client speed turns out to be visible.

}  // namespace pd2r::skills

namespace {

// PD2's leap speed: a monster's monstats Run << 8, a player's 0xb00 + calc2 * 0x50 (calc2 is 80 or 60 plus
// item_leap_speed / 4), never below 0xb00. D2R's LeapStart scales the run speed by calc2 % instead.
// shortcut: PD2's client half uses 0xa00 + calc2 * 0x29; both halves take the server's speed here so the client's
// path matches the server's. Upgrade if PD2's client speed turns out to be visible.
auto LeapVelocity(Unit* unit, const uint8_t* skill, int32_t skillId, int32_t level) noexcept -> int32_t {
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	if (At<int32_t>(unit, d2r::rva::UnitType) == 1) {
		const uint8_t* mon = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::rva::MonStatsRecord)(
			dataFlag, At<int32_t>(unit, d2r::rva::UnitClassId));
		return mon != nullptr ? At<int16_t>(mon, d2r::rva::MonRun) << 8 : 0;
	}
	const int32_t v = 0xb00 + d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, d2r::rva::SkCalc2), skillId, level) * 0x50;
	return v > 0xb00 ? v : 0xb00;
}


}  // namespace

namespace pd2r::skills {

void SetSkillVelocity(Unit* unit, UnitSkill* uskill, int32_t velocity) noexcept {
	void* path = At<void*>(unit, d2r::rva::UnitPath);
	if (path == nullptr || velocity <= 0) {
		return;
	}
	const uint32_t flags = Flags(uskill);
	Flags(uskill) = flags & ~0x1000u;  // PathSetVelocity refuses while 0x1000 is set; LeapStart clears it the same way
	d2r::Engine<void (*)(void*, int32_t) noexcept>(d2r::rva::PathSetVelocity)(path, velocity);
	Flags(uskill) = flags;
}

// D2R's start function (srvst 40 / cltst 29) has already run LeapStart by the time srvdo / cltdo see the leap, with the
// run speed scaled by calc2 as a percent (80 here), and the engine puts the run speed back a frame later. So PD2's speed
// is held on every call while the path is the leap path (type 9), not set once on the start frame (#1788).

}  // namespace pd2r::skills

namespace {

// D2R's start function (srvst 40 / cltst 29) has already run LeapStart by the time srvdo / cltdo see the leap, with the
// run speed scaled by calc2 as a percent (80 here), and the engine puts the run speed back a frame later. So PD2's speed
// is held on every call while the path is the leap path (type 9), not set once on the start frame (#1788).
void HoldLeapVelocity(Unit* unit, UnitSkill* uskill, int32_t velocity) noexcept {
	const void* path = At<void*>(unit, d2r::rva::UnitPath);
	if (path != nullptr && At<int32_t>(path, 0x50) == 9 && At<int32_t>(path, 0xa0) != velocity) {
		SetSkillVelocity(unit, uskill, velocity);
	}
}


struct LeapHit {
	UnitSkill* uskill;
	int32_t skillId;
	int32_t level;
	int64_t hits;
};

// One victim of the landing: D2R's own Leap Attack hit (to-hit roll with the skill's ToHit, weapon damage + calc1 %)
// aimed at it through the skill target and the path target, which is where 0x56ad90 looks.

// One victim of the landing: D2R's own Leap Attack hit (to-hit roll with the skill's ToHit, weapon damage + calc1 %)
// aimed at it through the skill target and the path target, which is where 0x56ad90 looks.
auto LeapHitOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	auto* hit = static_cast<LeapHit*>(area->arg);
	++hit->hits;  // units the hit is rolled on, not hits that land
	Unit* unit = area->unit;
	void* path = At<void*>(unit, d2r::rva::UnitPath);
	auto* target = reinterpret_cast<int32_t*>(hit->uskill + d2r::rva::USkillTargetType);
	const int32_t savedType = target[0], savedId = target[1];
	Unit* savedPathTarget = At<Unit*>(path, d2r::rva::PathTargetUnit);
	const auto setPathTarget = d2r::Engine<void (*)(void*, Unit*) noexcept>(d2r::rva::PathSetTargetUnit);
	target[0] = At<int32_t>(victim, d2r::rva::UnitType);
	target[1] = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(d2r::rva::UnitId)(victim, __FILE__, __LINE__);
	setPathTarget(path, victim);
	LeapAttackLeech(unit, victim);  // PD2's landing hit: life leech / 3 (#2009, skill/leech.cpp)
	d2r::Engine<void (*)(void*, Unit*, int32_t, int32_t) noexcept>(d2r::rva::SkillMeleeHit)(area->game, unit, hit->skillId,
	                                                                                       hit->level);
	LeapAttackLeech(nullptr, nullptr);
	setPathTarget(path, savedPathTarget);
	target[0] = savedType;
	target[1] = savedId;
	return 1;
}

// PD2 srvdofunc 153. The unit skill's flags pick the phase: 0x80 start the leap, 0x100 in the air (land), else wait.

// PD2 srvdofunc 153. The unit skill's flags pick the phase: 0x80 start the leap, 0x100 in the air (land), else wait.
auto SrvDoLeap(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	UnitSkill* uskill = skill != nullptr ? UnitSkillOf(unit, skillId) : nullptr;
	if (uskill == nullptr) {
		return 0;
	}
	const uint32_t flags = Flags(uskill);
	if ((flags & 0x100) == 0) {
		if ((flags & 0x80) == 0) {
			Flags(uskill) = 0x1000;
		} else if (d2r::Engine<int8_t (*)(Unit*, UnitSkill*) noexcept>(LeapStart)(unit, uskill) != 0) {
			SetSkillVelocity(unit, uskill, LeapVelocity(unit, skill, skillId, level));
			d2r::Engine<int32_t (*)(void*, Unit*, int32_t, int32_t) noexcept>(PathSteer)(At<void*>(unit, UnitPath), unit, 0, 1);
		}
		return 1;
	}
	if (d2r::Engine<int32_t (*)(Game*, Unit*, UnitSkill*) noexcept>(LeapLanded)(game, unit, uskill) == 0) {
		HoldLeapVelocity(unit, uskill, LeapVelocity(unit, skill, skillId, level));
		return 1;
	}
	const int32_t radius = d2r::EvalCalc(game, unit, skill, SkCalc3, skillId, level);
	int64_t hits = 0;
	if (radius > 0 && skillId == SkillLeap) {
		d2r::LeapKnockback knockback;
		uint8_t scratch[0x20] {};  // srvdo 77's second block: only its byte +0x18 (0) is read
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, d2r::LeapKnockback*, int32_t, void*) noexcept>(
			AreaKnockback)(game, unit, 0, 0, radius, &knockback, 0, scratch);
	} else if (radius > 0) {
		d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit), 0, 0};
		LeapHit hit {uskill, skillId, level, 0};
		d2r::Engine<AreaFn>(AreaUnits)(game, unit, &center, radius, AreaUnitMask, LeapHitOne, &hit, 0);
		hits = hit.hits;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(RemoveUnitEvent)(game, unit, 1, 0);
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(AddUnitEvent)(
		game, unit, 1, At<int32_t>(game, GameFrame) + 4, 0, 0, 0);
	++g_leaps;
	return 1;
}

// 93847: 0x22e8f0, void (unit, unitSkill): the client leap's jump arc (gravity and lift), timed from the path's
// velocity at the call. D2R's cltdo 43 runs it on the start frame with LeapStart's speed.

}  // namespace

namespace pd2r::skills {

// 93847: 0x22e8f0, void (unit, unitSkill): the client leap's jump arc (gravity and lift), timed from the path's
// velocity at the call. D2R's cltdo 43 runs it on the start frame with LeapStart's speed.
sigscan::Entry LeapArc {"ClientLeapArc", "skills", sigscan::Kind::Code, 0, 0, "48 8b c4 48 89 58 20 57 48 83 ec 70"};

// PD2 cltdofunc 97: D2R's Leap client half (cltdo 43) with PD2's speed on the start frame.

}  // namespace pd2r::skills

namespace {

// PD2 cltdofunc 97: D2R's Leap client half (cltdo 43) with PD2's speed on the start frame.
auto CltDoLeap(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	if (unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::api.unitDataFlag(unit), skillId);
	UnitSkill* uskill = skill != nullptr ? UnitSkillOf(unit, skillId) : nullptr;
	if (uskill == nullptr) {
		return 0;
	}
	const uint32_t flags = Flags(uskill);
	const int32_t ran = d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(d2r::rva::CltDoLeap)(unit, skillId);
	if ((flags & 0x2) == 0 && (flags & 0x80) != 0) {
		const int32_t velocity = LeapVelocity(unit, skill, skillId, level);
		SetSkillVelocity(unit, uskill, velocity);
		// The arc was timed for LeapStart's slower speed, so the unit landed mid-arc and bounced (#1961): time it again.
		if (LeapArc.state == sigscan::State::Ok) {
			d2r::Engine<void (*)(Unit*, UnitSkill*) noexcept>(LeapArc)(unit, uskill);
		}
		++g_leapClients;
	}
	HoldLeapVelocity(unit, uskill, LeapVelocity(unit, skill, skillId, level));
	return ran;
}

// --- Inferno, Arctic Blast (PD2 srvdofunc 182, cltdofunc 111) ----------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 182 at 0x103b4e40, cltdo 111 at 0x103abce0 (docs/research/skills/druid-w21.md). A player
// sends calc3 flames of range calc1 in a line across the aim; a monster keeps D2R's Inferno (srvdo 19).

}  // namespace

PD2R_SLOT(8, SrvDo, 172, static_cast<SrvDoFn>(SrvDoLeap), "leap")
PD2R_SLOT(9, CltDo, 115, static_cast<CltDoFn>(CltDoLeap), "leap")
