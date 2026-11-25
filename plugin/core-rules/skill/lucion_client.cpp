// lucion_client
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

// PD2's client ring call (0x1036d370): `count` of the skill's client missile `column` at x/y, for a valid row and a
// count above 0.
auto ClientRing(Unit* unit, const uint8_t* skill, int column, int calcColumn, int32_t x, int32_t y, int32_t skillId,
                int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const int32_t missile = At<int16_t>(skill, column);
	const int32_t count = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, calcColumn), skillId, level);
	if (!d2r::ValidMissile(dataFlag, missile) || count <= 0) {
		return 0;
	}
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, missile, x, y, skillId, level);
	return Ring(count, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		return d2r::api.clientCreateMissile(&params, 0) != nullptr;
	});
}

// PD2 cltdo 113 (LucionBlink): a ring of cltcalc1 cltmissilea at the caster.

// PD2 cltdo 113 (LucionBlink): a ring of cltcalc1 cltmissilea at the caster.
auto CltDoLucionBlink(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t* skill = unit != nullptr ? d2r::api.getSkillRecord(d2r::api.unitDataFlag(unit), skillId) : nullptr;
	int32_t x = 0, y = 0;
	if (skill == nullptr || !d2r::UnitXY(unit, &x, &y)) {
		return 0;
	}
	const int32_t created = ClientRing(unit, skill, SkCltMissileA, SkCltCalc1, x, y, skillId, level);
	return 1;
}

// The dashing units whose landing ring is still due: each start arms one landing, which fires when the unit reaches
// the path's target. The entry holds that target: a call with another target is a new dash (the last was cut short, or
// the unit died and its address came back), and with no free slot the oldest entry is taken over.
// shortcut: 8 dashers at once; upgrade if more than 8 dash on one screen.

// The dashing units whose landing ring is still due: each start arms one landing, which fires when the unit reaches
// the path's target. The entry holds that target: a call with another target is a new dash (the last was cut short, or
// the unit died and its address came back), and with no free slot the oldest entry is taken over.
// shortcut: 8 dashers at once; upgrade if more than 8 dash on one screen.
struct Dasher {
	Unit* unit;
	uint16_t targetX, targetY;
};

Dasher g_dashers[8] {};

size_t g_dasherOldest = 0;

// PD2 cltdo 114 (LucionDash): PD2's flat dash client (not D2R's CltDoLeap, whose jump arc lifts the unit) at PD2's
// speed (monstats Run << 9, a player's 0xb00 + calc2 * 0x50), a ring of cltcalc1 cltmissilea on the start frame, and a
// ring of cltcalc2 cltmissileb within 2 subtiles of the path's target.
// shortcut: PD2's in-flight cltmissilec trail (a D2Client call, 0x103a1530) is not ported; the server's srvmissilec
// trail shows in its place. Upgrade if luciondashtrail looks missing in play.

// PD2 cltdo 114 (LucionDash): PD2's flat dash client (not D2R's CltDoLeap, whose jump arc lifts the unit) at PD2's
// speed (monstats Run << 9, a player's 0xb00 + calc2 * 0x50), a ring of cltcalc1 cltmissilea on the start frame, and a
// ring of cltcalc2 cltmissileb within 2 subtiles of the path's target.
// shortcut: PD2's in-flight cltmissilec trail (a D2Client call, 0x103a1530) is not ported; the server's srvmissilec
// trail shows in its place. Upgrade if luciondashtrail looks missing in play.
auto CltDoLucionDash(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = unit != nullptr ? d2r::api.unitDataFlag(unit) : 0;
	const uint8_t* skill = unit != nullptr ? d2r::api.getSkillRecord(dataFlag, skillId) : nullptr;
	UnitSkill* uskill = skill != nullptr ? UnitSkillOf(unit, skillId) : nullptr;
	if (uskill == nullptr) {
		return 0;
	}
	const uint32_t flags = Flags(uskill);
	// D2R's leap client (CltDoLeap) lifts the unit on a jump arc PD2's flat dash lacks (#1205): the model rose off
	// screen while the server's unit stayed at the player. The path follows the server, so the call is skipped.
	const int32_t ran = 1;
	int32_t x = 0, y = 0;
	if (!d2r::UnitXY(unit, &x, &y)) {
		return ran;
	}
	void* path = At<void*>(unit, UnitPath);
	const uint16_t targetX = path != nullptr ? At<uint16_t>(path, 0x10) : 0, targetY = path != nullptr ? At<uint16_t>(path, 0x12) : 0;
	Dasher* const armed = std::find_if(std::begin(g_dashers), std::end(g_dashers), [&](const Dasher& d) { return d.unit == unit; });
	// The leap client set flag 0x100 after its first call; without it the start frame is the first call per dash.
	if ((flags & 0x80) != 0 && (armed == std::end(g_dashers) || armed->targetX != targetX || armed->targetY != targetY)) {
		int32_t velocity = 0;
		if (At<int32_t>(unit, UnitType) == UnitMonster) {
			const uint8_t* mon =
				d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(dataFlag, At<int32_t>(unit, UnitClassId));
			velocity = mon != nullptr ? At<int16_t>(mon, MonRun) << 9 : 0;
		} else {
			velocity = 0xb00 + d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, SkCltCalc2), skillId, level) * 0x50;
			velocity = velocity > 0xb00 ? velocity : 0xb00;
		}
		SetSkillVelocity(unit, uskill, velocity);
		const int32_t created = ClientRing(unit, skill, SkCltMissileA, SkCltCalc1, x, y, skillId, level);
		Dasher* slot = armed;
		if (slot == std::end(g_dashers)) {
			slot = std::find_if(std::begin(g_dashers), std::end(g_dashers), [](const Dasher& d) { return d.unit == nullptr; });
		}
		if (slot == std::end(g_dashers)) {
			slot = &g_dashers[g_dasherOldest++ % std::size(g_dashers)];
		}
		*slot = {unit, targetX, targetY};
		return ran;
	}
	if (armed == std::end(g_dashers) || path == nullptr) {
		return ran;
	}
	const int32_t dx = x - targetX, dy = y - targetY;  // the path's target point
	if (dx * dx + dy * dy <= 4) {
		*armed = {};
		const int32_t created = ClientRing(unit, skill, SkCltMissileA + 2, SkCltCalc2, x, y, skillId, level);
	}
	return ran;
}

// --- Unsummon gives back the Iron Golem's item (PD2 srvdo 4, #1873) -------------------------------------------------
// Read from ProjectDiablo.dll 0x103ae650 (installed 0x103ae6c0), PD2's srvdo 4 over 1.13c's D2Game +0xa2ea0: when the
// used skill's target id is one of the caster's pets (D2Game +0x96600) and that monster is class 291 (irongolem), it
// copies the first item of the golem's inventory (D2Game +0x107e0: serialized and rebuilt with its socketed items),
// drops the copy at a free spot by the golem (D2Game +0xe930, +0xe990), then runs the vanilla unsummon (+0x97b50),
// which kills the golem and its original item with it. With no room or spot it returns 0 and the golem stays.
// D2R 93847's srvdo 4 (0x4305c0) is that vanilla unsummon, so this wraps it. The SDK's item services reach player
// storage only (no monster inventory, no ground destination for an existing item), so the steps are D2R's own calls,
// every one found by signature.
// shortcut: PD2 first walks off a golem tile blocked by walls or doors (0x10389c20, mask 0x805); D2R's spot search
// alone is used, the same final step. Upgrade if a golem ever stands in a wall.

}  // namespace

PD2R_SLOT(46, CltDo, 113, static_cast<CltDoFn>(CltDoLucionBlink), "lucion blink")
PD2R_SLOT(47, CltDo, 114, static_cast<CltDoFn>(CltDoLucionDash), "lucion dash")
