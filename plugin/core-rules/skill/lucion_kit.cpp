// lucion_kit
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

// Lucion's kit (#487), below
constexpr int32_t LucionNova = 640, LucionDash = 644, LucionPunish = 645;  // merged ids

auto SrvDoLucionNova(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

auto SrvDoLucionDash(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

auto SrvDoLucionPunish(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t;

// srvdofunc 190 serves the PD2 do functions that have no free slot of their own, by skill id (docs/features/core-rules.md). The
// dispatcher is the toolkit's skill_router (a missing id logs a sampled warning there); this lists our routes.

// --- Lucion's kit (PD2 srvdofunc 184 Nova, 185 Dash, 186 Punish -> router 190; Dash's srvstfunc 76 -> 40) ------------
// Read from ProjectDiablo.dll: srvdo 184 at 0x103b5110, 185 at 0x103b52b0, 186 at 0x103b56d0, srvst 76 at 0x103b6aa0
// (docs/pd2/uber-bosses-dll.md, section 5). PD2 reads Lucion's AI params and the fight's sub-step (game
// +0x262b); core-events' lucion_ai.cpp keeps them in D2R's per-game counters, read here by the same keys.
constexpr int32_t Lucion = 1129;  // PD2 1112 (0x458), merged id

constexpr uint32_t LucionStateKey = 0x55424c41, LucionParamKey = 0x55424c42, LucionSubStepKey = 0x55424c53;


auto LucionCounter(Game* game, uint32_t key) noexcept -> int32_t {
	return *d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(d2r::rva::GameCounter)(
		reinterpret_cast<uint8_t*>(game) + d2r::rva::GameCounters, &key);
}

// The directions of D2's 64 a Nova fires, as bits: from seed's direction on, with up to 5 gaps of 6 directions (each
// on a roll of the sequence below 15%, 2% less per gap, and only after 8 fired since the last).

// The directions of D2's 64 a Nova fires, as bits: from seed's direction on, with up to 5 gaps of 6 directions (each
// on a roll of the sequence below 15%, 2% less per gap, and only after 8 fired since the last).
constexpr auto LucionNovaDirections(uint32_t seed) noexcept -> uint64_t {
	uint64_t fired = 0;
	int32_t dir = static_cast<int32_t>(seed & 63), left = 64, gaps = 5, chance = 15, run = 0;
	while (left > 0) {
		if (gaps > 0 && run == 0) {
			seed = CastStep(seed);
			if (static_cast<int32_t>(seed) % 100 < chance) {
				dir = (dir + 6) & 63;
				run = 8;
				--gaps;
				chance -= 2;
				left -= 6;
				continue;
			}
		}
		run -= run > 0 ? 1 : 0;
		fired |= uint64_t {1} << dir;
		dir = (dir + 1) & 63;
		--left;
	}
	return fired;
}

static_assert(std::popcount(LucionNovaDirections(CastStep(1234))) % 6 == 4 &&
              std::popcount(LucionNovaDirections(CastStep(1234))) >= 34);  // 64 less 6 per gap

// PD2 srvdo 184: the Nova's ring from Lucion (a monster), seeded by his AI param 1, which the AI sets before the first
// Nova and keeps for the second, so both leave the same gaps. The second (AI state 8) fires srvmissileb, else
// srvmissilea.

// PD2 srvdo 184: the Nova's ring from Lucion (a monster), seeded by his AI param 1, which the AI sets before the first
// Nova and keeps for the second, so both leave the same gaps. The second (AI state 8) fires srvmissileb, else
// srvmissilea.
auto SrvDoLucionNova(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr || At<int32_t>(unit, UnitType) != UnitMonster) {
		return 0;
	}
	const bool second = LucionCounter(game, LucionStateKey) == 8;
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA + (second ? 2 : 0));
	int32_t x = 0, y = 0;
	if (!d2r::ValidMissile(dataFlag, missile) || !d2r::UnitXY(unit, &x, &y)) {
		return 0;
	}
	const uint64_t fired = LucionNovaDirections(CastStep(static_cast<uint32_t>(LucionCounter(game, LucionParamKey))));
	const auto* dx = reinterpret_cast<const int32_t*>(d2r::imageBase + DirectionX);
	const auto* dy = reinterpret_cast<const int32_t*>(d2r::imageBase + DirectionY);
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, missile, x, y, skillId, level);
	int32_t created = 0;
	for (int32_t dir = 0; dir < 64; ++dir) {
		if ((fired >> dir & 1) != 0) {
			params.targetX = dx[dir];
			params.targetY = dy[dir];
			created += d2r::api.createMissile(game, &params) != nullptr ? 1 : 0;
		}
	}
	return 1;
}

// PD2 srvdo 186: calc1 srvmissilea standing in a ring of radius calc2 round the skill's target point, at angles
// 2 * 3.2 * i / calc1 (PD2's constant is 3.2, not pi, so the ring overlaps itself a little). Lucion's second and third
// Punish of a frenzy (sub-step 2, 3) double and triple the count and widen the ring by 3 and 6.

// PD2 srvdo 186: calc1 srvmissilea standing in a ring of radius calc2 round the skill's target point, at angles
// 2 * 3.2 * i / calc1 (PD2's constant is 3.2, not pi, so the ring overlaps itself a little). Lucion's second and third
// Punish of a frenzy (sub-step 2, 3) double and triple the count and widen the ring by 3 and 6.
auto SrvDoLucionPunish(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	const int32_t missile = skill != nullptr ? At<int16_t>(skill, SkSrvMissileA) : -1;
	if (skill == nullptr || !d2r::ValidMissile(dataFlag, missile)) {
		return 0;
	}
	int32_t tx = 0, ty = 0;
	d2r::api.skillTargetXY(game, unit, &tx, &ty);  // PD2 casts round 0, 0 without one, too
	int32_t count = d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level);
	int32_t radius = d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level);
	const int32_t sub = LucionCounter(game, LucionSubStepKey) & 0xff;  // PD2's is a byte
	if (At<int32_t>(unit, UnitType) == UnitMonster && At<int32_t>(unit, UnitClassId) == Lucion && (sub == 2 || sub == 3)) {
		count *= sub;
		radius += 3 * (sub - 1);
	}
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, missile, 0, 0, skillId, level);
	int32_t created = 0;
	for (int32_t i = 0; i < count; ++i) {
		const double angle = (static_cast<double>(i) + i) * 3.2 / count;
		params.x = static_cast<int32_t>(std::cos(angle) * radius) + tx;
		params.y = static_cast<int32_t>(std::sin(angle) * radius) + ty;
		created += d2r::api.createMissile(game, &params) != nullptr ? 1 : 0;
	}
	return 1;
}

// PD2's dash speed: a monster's monstats Run << 9, a player's 0xb00 + calc2 * 0x50, never below 0xb00

// PD2's dash speed: a monster's monstats Run << 9, a player's 0xb00 + calc2 * 0x50, never below 0xb00
auto LucionDashVelocity(Game* game, Unit* unit, const uint8_t* skill, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (At<int32_t>(unit, UnitType) == UnitMonster) {
		const uint8_t* mon = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
			d2r::DataFlag(game), At<int32_t>(unit, UnitClassId));
		return mon != nullptr ? At<int16_t>(mon, MonRun) << 9 : 0;
	}
	const int32_t v = 0xb00 + d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level) * 0x50;
	return v > 0xb00 ? v : 0xb00;
}

// PD2 srvdo 185, a leap with no attack to the spot its start picked (srvst 76; the merge points it at D2R's Leap
// start, 40). Start frame (unit skill flag 0x80): D2R's leap path at PD2's speed and a ring of calc1 srvmissilea at
// the caster. In flight: a srvmissilec trail at the caster. On landing: a ring of calc2 srvmissileb there, and the
// mode ends a frame later.

// PD2 srvdo 185, a leap with no attack to the spot its start picked (srvst 76; the merge points it at D2R's Leap
// start, 40). Start frame (unit skill flag 0x80): D2R's leap path at PD2's speed and a ring of calc1 srvmissilea at
// the caster. In flight: a srvmissilec trail at the caster. On landing: a ring of calc2 srvmissileb there, and the
// mode ends a frame later.
auto SrvDoLucionDash(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	UnitSkill* uskill = skill != nullptr ? CurrentUnitSkill(unit) : nullptr;
	int32_t x = 0, y = 0;
	if (uskill == nullptr || !d2r::UnitXY(unit, &x, &y)) {
		return 0;
	}
	// PD2's ring call (0x1036d250) only for a valid row and a count above 0
	const auto ring = [&](int column, int calc) {
		const int32_t missile = At<int16_t>(skill, column);
		const int32_t count = d2r::EvalCalc(game, unit, skill, calc, skillId, level);
		return d2r::ValidMissile(dataFlag, missile) && count > 0
		           ? ServerRing(game, unit, missile, x, y, count, skillId, level)
		           : 0;
	};
	const uint32_t flags = Flags(uskill);
	if ((flags & 0x100) == 0) {
		if ((flags & 0x80) == 0) {
			Flags(uskill) = 0x1000;
			return 1;
		}
		if (d2r::Engine<int8_t (*)(Unit*, UnitSkill*) noexcept>(LeapStart)(unit, uskill) == 0) {
			return 0;
		}
		const int32_t velocity = LucionDashVelocity(game, unit, skill, skillId, level);
		SetSkillVelocity(unit, uskill, velocity);
		d2r::Engine<int32_t (*)(void*, Unit*, int32_t, int32_t) noexcept>(PathSteer)(At<void*>(unit, UnitPath), unit, 0, 1);
		const int32_t created = ring(SkSrvMissileA, SkCalc1);
		return 1;
	}
	if (d2r::Engine<int32_t (*)(Game*, Unit*, UnitSkill*) noexcept>(LeapLanded)(game, unit, uskill) != 0) {
		const int32_t created = ring(SkSrvMissileA + 2, SkCalc2);
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(RemoveUnitEvent)(game, unit, 1, 0);
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(AddUnitEvent)(
			game, unit, 1, At<int32_t>(game, GameFrame) + 1, 0, 0, 0);
		return 0;
	}
	const int32_t trail = At<int16_t>(skill, SkSrvMissileA + 4);
	int32_t tx = 0, ty = 0;
	if (d2r::ValidMissile(dataFlag, trail) && d2r::api.skillTargetXY(game, unit, &tx, &ty) != 0) {
		d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY, trail, x, y, skillId, level);
		params.targetX = tx;
		params.targetY = ty;
		d2r::api.createMissile(game, &params);
	}
	return 0;
}

// --- Lucion's client halves (PD2 cltdo 113 at 0x103ac1e0, cltdo 114 at 0x103ac270; #488) ---------------------------

// PD2's client ring call (0x1036d370): `count` of the skill's client missile `column` at x/y, for a valid row and a
// count above 0.

}  // namespace

PD2R_ROUTE(50, LucionNova, SrvDoLucionNova)
PD2R_ROUTE(51, LucionDash, SrvDoLucionDash)
PD2R_ROUTE(52, LucionPunish, SrvDoLucionPunish)
