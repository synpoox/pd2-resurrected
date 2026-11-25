// inferno
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

// --- Inferno, Arctic Blast (PD2 srvdofunc 182, cltdofunc 111) ----------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 182 at 0x103b4e40, cltdo 111 at 0x103abce0 (docs/research/skills/druid-w21.md). A player
// sends calc3 flames of range calc1 in a line across the aim; a monster keeps D2R's Inferno (srvdo 19).
std::atomic<uint32_t> g_channels {0};

std::atomic<uint32_t> g_channelClients {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t StateInferno = 12;  // states.txt inferno, on while the skill channels; the same id in PD2 and D2R

// PD2's 0x1037d860: the target point moved to `dist` subtiles from the caster, on the same line.

// PD2's 0x1037d860: the target point moved to `dist` subtiles from the caster, on the same line.
void Reach(int32_t ux, int32_t uy, int32_t* tx, int32_t* ty, int32_t dist) noexcept {
	const int32_t dx = *tx - ux, dy = *ty - uy;
	const double length = std::sqrt(static_cast<double>(dx * dx + dy * dy));
	if (length != 0) {
		*tx = static_cast<int32_t>(static_cast<double>(dx) * dist / length + ux);
		*ty = static_cast<int32_t>(static_cast<double>(dy) * dist / length + uy);
	}
}


struct Line {
	int32_t x, y, stepX, stepY;  // the first point and the step to the next
};

// PD2's 0x1037d780: `count` points one small step apart through the target, at right angles to the aim (Multiple
// Shot's vector steps, the other way round). Under 2 subtiles PD2's code puts the length in y, not 4 * y; kept.

// PD2's 0x1037d780: `count` points one small step apart through the target, at right angles to the aim (Multiple
// Shot's vector steps, the other way round). Under 2 subtiles PD2's code puts the length in y, not 4 * y; kept.
constexpr auto ChannelLine(int32_t ux, int32_t uy, int32_t tx, int32_t ty, int32_t count) noexcept -> Line {
	int32_t ax = ux - tx, ay = uy - ty, length = ax * ax + ay * ay;
	if (length < 4) {
		ax *= 4;
		ay = length;
		length = length * length + ax * ax;
	}
	if (length < 16) {
		ax *= 2;
		ay *= 2;
	}
	int32_t sx = ay, sy = -ax;
	while (sx * sx + sy * sy > 3) {
		sx /= 2;
		sy /= 2;
	}
	return {tx - sx * count / 2, ty - sy * count / 2, sx, sy};
}

static_assert(ChannelLine(0, 0, 10, 0, 3).x == 10 && ChannelLine(0, 0, 10, 0, 3).y == -1);

static_assert(ChannelLine(0, 0, 10, 0, 3).stepX == 0 && ChannelLine(0, 0, 10, 0, 3).stepY == 1);

static_assert(ChannelLine(0, 0, 0, 10, 2).x == 1 && ChannelLine(0, 0, 0, 10, 2).stepX == -1);

// shortcut: PD2 then re-times the caster's frame and mode-end events from the next whole frame (0x1037a7c0); D2R's
// events are left as they are. Upgrade if the channel's cast rate differs from PD2's in play (docs/deviations.md).

// shortcut: PD2 then re-times the caster's frame and mode-end events from the next whole frame (0x1037a7c0); D2R's
// events are left as they are. Upgrade if the channel's cast rate differs from PD2's in play (docs/deviations.md).
auto SrvDoChannel(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	if (At<int32_t>(unit, UnitType) == 1) {
		return VanillaSlot<SrvDoFn>(SrvDoTable, SrvDoInferno)(game, unit, skillId, level);
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	int32_t tx = 0, ty = 0, ux = 0, uy = 0;
	if (skill == nullptr || d2r::Engine<void* (*)(Unit*) noexcept>(CurrentSkill)(unit) == nullptr ||
	    !d2r::ValidMissile(dataFlag, At<int16_t>(skill, SkSrvMissileA)) ||
	    d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0 || !d2r::UnitXY(unit, &ux, &uy)) {
		return 0;
	}
	const int32_t range = std::max(d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level), 1);
	const int32_t count = ClampCount(d2r::EvalCalc(game, unit, skill, SkCalc3, skillId, level));
	Reach(ux, uy, &tx, &ty, count / 2 + 8);
	const Line line = ChannelLine(ux, uy, tx, ty, count);
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = MissileChannel;
	params.owner = unit;
	params.origin = unit;
	params.missile = At<int16_t>(skill, SkSrvMissileA);
	params.skill = skillId;
	params.skillLevel = level;
	params.range = range;
	int32_t created = 0;
	for (int32_t i = 0; i < count; ++i) {
		params.targetX = line.x + i * line.stepX;
		params.targetY = line.y + i * line.stepY;
		created += d2r::api.createMissile(game, &params) != nullptr ? 1 : 0;
	}
	++g_channels;
	return 1;
}

// Client half: the same line from cltmissilea, 40% of the time pushed calc3 further out; each flame is drawn with
// chance 2 / calc3 and lives calc1 - 1 or calc1 frames. Then D2R's own cltdo 24 ending: another player's caster under
// the inferno state is held in the channel frames. PD2 also rolls a velocity per flame (Vel - 6 + 0-7, halved on the
// first), but its flags leave out 4, the bit that makes the create read it, so its flames keep Vel; so do these.
// shortcut: PD2's ending has no local-player check; this keeps D2R's. Upgrade if the caster's own channel animation
// differs from PD2's in play.
// shortcut: PD2's post-create call (0x103a45c0 with calc1) is taken to be D2R's SetMissileAnimRange. Upgrade when that
// D2Common pointer is resolved, or if the flames animate differently from PD2's.

// Client half: the same line from cltmissilea, 40% of the time pushed calc3 further out; each flame is drawn with
// chance 2 / calc3 and lives calc1 - 1 or calc1 frames. Then D2R's own cltdo 24 ending: another player's caster under
// the inferno state is held in the channel frames. PD2 also rolls a velocity per flame (Vel - 6 + 0-7, halved on the
// first), but its flags leave out 4, the bit that makes the create read it, so its flames keep Vel; so do these.
// shortcut: PD2's ending has no local-player check; this keeps D2R's. Upgrade if the caster's own channel animation
// differs from PD2's in play.
// shortcut: PD2's post-create call (0x103a45c0 with calc1) is taken to be D2R's SetMissileAnimRange. Upgrade when that
// D2Common pointer is resolved, or if the flames animate differently from PD2's.
auto CltDoChannel(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	int32_t tx = 0, ty = 0, ux = 0, uy = 0;
	if (skill == nullptr || !d2r::ValidMissile(dataFlag, At<int16_t>(skill, SkCltMissileA)) ||
	    d2r::api.clientSkillTargetXY(unit, &tx, &ty) == 0 || !d2r::UnitXY(unit, &ux, &uy)) {
		return 0;
	}
	const auto calc = [&](int field) {
		return d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, field), skillId, level);
	};
	const int32_t range = std::max(calc(SkCalc1), 1);
	const int32_t count = ClampCount(calc(SkCalc3));
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	// signed remainders, as PD2's IDIV takes them: a negative roll always passes the < 40 and < 2 tests
	const int32_t further = static_cast<int32_t>(d2r::Roll(seed)) % 100 < 40 && count > 1 ? count : 0;
	Reach(ux, uy, &tx, &ty, count / 2 + further + 8);
	const Line line = ChannelLine(ux, uy, tx, ty, count);
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = MissileChannel;
	params.owner = unit;
	params.origin = unit;
	params.missile = At<int16_t>(skill, SkCltMissileA);
	params.x = ux;
	params.y = uy;
	params.skill = skillId;
	params.skillLevel = level;
	const uint8_t* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(dataFlag,
	                                                                                                  params.missile);
	int32_t drawn = 0;
	for (int32_t i = 0; i < count; ++i) {
		const int32_t roll = static_cast<int32_t>(d2r::Roll(seed));
		if (roll % count >= 2) {
			continue;
		}
		params.range = std::max(range + roll % 2 - 1, 1);  // a negative roll takes 2 off (signed, as in PD2)
		params.targetX = line.x + i * line.stepX;
		params.targetY = line.y + i * line.stepY;
		Unit* flame = d2r::api.clientCreateMissile(&params, 0);
		if (flame != nullptr && record != nullptr) {
			d2r::Engine<void (*)(Unit*, uint32_t) noexcept>(SetMissileAnimRange)(
				flame, (static_cast<uint32_t>(At<uint8_t>(record, MsAnimSpeed)) << 8) / range);
		}
		drawn += flame != nullptr ? 1 : 0;
	}
	Unit* local = d2r::Engine<Unit* (*)(uint32_t) noexcept>(ClientPlayer)(
		d2r::Engine<uint32_t (*)() noexcept>(LocalPlayerSlot)());
	if (local != unit && d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(unit, StateInferno) != 0) {
		if (At<void*>(unit, UnitSeqAnim) == nullptr) {
			d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetUnitFrame)(
				unit, d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitFrame)(unit) - 2);
		} else {
			// PD2 0x103abfae: #10542(unit, 1), frame 1 of its 8; D2R sets the speed below, as #10542 does (0x500)
			d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetUnitFrames)(unit, 1);
			d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetUnitAnimSpeed)(unit, 5);
		}
	}
	++g_channelClients;
	return 1;
}

// --- The channel take-over (PD2's gate A, ProjectDiablo.dll 0x10373290; docs/research/skills/druid-w21.md) ---------------
// PD2 keys two rules on the busy player's current skill: Inferno / Arctic Blast (srvdofunc 182) give way at once to
// any request that carries a skill; Blade Fury (48) gives way to any other skill and refuses another Blade Fury, so its
// channel goes on. Everything else is D2R's own gate.

}  // namespace

PD2R_SLOT(18, SrvDo, 182, static_cast<SrvDoFn>(SrvDoChannel), "channel")
PD2R_SLOT(19, CltDo, 111, static_cast<CltDoFn>(CltDoChannel), "channel")
