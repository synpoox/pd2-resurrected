// dragon_flight
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

// --- Dragon Flight (PD2 srvdofunc 178; docs/research/skills/w31-dragon-flight-psychic-hammer.md) -------------------------
// Read from ProjectDiablo.dll: srvdo 178 at 0x103b4180.
std::atomic<uint32_t> g_dragonFlights {0};

std::atomic<uint32_t> g_dragonFlightHits {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t ProgressiveToHit = 325;  // itemstatcost progressive_tohit: PD2 and D2R add it to the skill's to-hit

// PD2 srvdofunc 178: the teleport core (refused: no kick), then one kick at the target in the same frame: to-hit =
// the skill's + progressive_tohit, damage% = calc2 on a hit, the release (srvprgfunc for the charges) and the hit.
// The hit is built the way D2R's Dragon Flight (srvdo 52) builds its second frame; 52 teleports on one frame and kicks
// on the next, with damage% from calc1. PD2's closing aurastate helper (0x1036cb90) is left out: it returns at once
// without aurastat1, and no row on 178 has one.
// shortcut: the target is D2R's skill target (PD2 reads the path's target unit), as for Holy Light. Upgrade if a cast
// in game kicks another unit than the one clicked (docs/deviations.md).

// PD2 srvdofunc 178: the teleport core (refused: no kick), then one kick at the target in the same frame: to-hit =
// the skill's + progressive_tohit, damage% = calc2 on a hit, the release (srvprgfunc for the charges) and the hit.
// The hit is built the way D2R's Dragon Flight (srvdo 52) builds its second frame; 52 teleports on one frame and kicks
// on the next, with damage% from calc1. PD2's closing aurastate helper (0x1036cb90) is left out: it returns at once
// without aurastat1, and no row on 178 has one.
// shortcut: the target is D2R's skill target (PD2 reads the path's target unit), as for Holy Light. Upgrade if a cast
// in game kicks another unit than the one clicked (docs/deviations.md).
auto SrvDoDragonFlight(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	if (skill == nullptr || target == nullptr) {
		return 0;
	}
	if (!TeleportCore(game, unit, skillId, level)) {
		return 0;
	}
	// srvdo 52 clears it at its teleport and sets it after the kick; cleared only once the teleport went, so a refusal
	// leaves it as it was
	d2r::api.setUnitSkillFlag(unit, 0);
	const int32_t progressive = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(unit, ProgressiveToHit, 0);
	const int32_t toHit =
		d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(SkillToHit)(unit, skillId, level) + progressive;
	d2r::LeapKnockback damage;
	using RollFn = uint16_t (*)(Game*, Unit*, Unit*, int32_t, uint8_t, int32_t) noexcept;
	damage.resultFlags = d2r::Engine<RollFn>(MeleeRoll)(game, unit, target, toHit, 0, 0);  // PD2 rolls with no always-hit
	int32_t percent = 0;
	const bool hit = (damage.resultFlags & ResultHit) != 0;
	if (hit) {
		percent = d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level);
		*reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&damage) + DmgPercent) = percent;
		d2r::Engine<void (*)(Game*, void*, Unit*, Unit*, int32_t, int32_t) noexcept>(MeleeFill)(game, &damage, unit,
		                                                                                      target, skillId, level);
	}
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*, uint8_t) noexcept>(QueueHit)(game, unit, target, &damage, 0x80);
	void* queued = d2r::Engine<void* (*)(Unit*, Unit*) noexcept>(QueuedHit)(unit, target);
	d2r::Engine<void (*)(Game*, Unit*, int32_t, void*) noexcept>(SrvPrgDispatch)(game, unit, skillId, queued);
	d2r::Engine<void (*)(Game*, Unit*, Unit*, int32_t, uint8_t) noexcept>(ApplyMeleeHit)(game, unit, target, 0, 1);
	if (d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) == 0) {
		d2r::api.setUnitSkillFlag(unit, 1);
	}
	const uint32_t resultFlags = damage.resultFlags;
	ReleaseDamage(damage);
	++g_dragonFlights;
	if (hit) {
		++g_dragonFlightHits;
	}
	return 1;
}


}  // namespace

PD2R_SLOT(52, SrvDo, 178, static_cast<SrvDoFn>(SrvDoDragonFlight), "dragon flight")
