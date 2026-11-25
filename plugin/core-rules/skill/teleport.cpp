// teleport
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

// --- Teleport (PD2 srvdofunc 155 -> router 190; docs/research/skills/sorceress-w26.md) -----------------------------------
// Read from ProjectDiablo.dll: srvdo 155 at 0x103b16d0, its teleport core at 0x1036cd20 (shared with Blood Warp).
std::atomic<uint32_t> g_teleportsRefused {0};

constexpr int32_t Teleport = 54, Blink = 429, LucionBlink = 642, MonBloodwarp = 682;  // merged ids

constexpr int32_t DisableTele = 260;  // merged states id of disable_tele (PD2 214, Rathma's prison)
// passive_fire/cold/ltng/pois_mastery, in the order PD2 sets them; the same itemstatcost ids in PD2 and D2R

// passive_fire/cold/ltng/pois_mastery, in the order PD2 sets them; the same itemstatcost ids in PD2 and D2R
constexpr int32_t Masteries[] = {329, 331, 330, 332};

// PD2's teleport core: refused under disable_tele, else D2R's Teleport (srvdo 27), which
// makes the core's level checks (levels.txt Teleport 0 refuses, 2 checks collision at the point).
// PD2's Hell-only level list (0x105de86c) is never filled, so it is left out.
// shortcut: PD2's "TeleportBlocked" chat line on a Teleport 0 refusal is left out: no ported level has Teleport 0.
// Upgrade when one does.

}  // namespace

namespace pd2r::skills {

// PD2's teleport core: refused under disable_tele, else D2R's Teleport (srvdo 27), which
// makes the core's level checks (levels.txt Teleport 0 refuses, 2 checks collision at the point).
// PD2's Hell-only level list (0x105de86c) is never filled, so it is left out.
// shortcut: PD2's "TeleportBlocked" chat line on a Teleport 0 refusal is left out: no ported level has Teleport 0.
// Upgrade when one does.
auto TeleportCore(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> bool {
	using namespace d2r::rva;
	if (d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, DisableTele) != nullptr) {
		g_teleportsRefused.fetch_add(1);
		return false;
	}
	return VanillaSlot<SrvDoFn>(SrvDoTable, SrvDoTeleport)(game, unit, skillId, level) != 0;
}

// PD2's debuff on one mastery stat: -(reduction + mastery% of it), the scaled part truncated (0x1037c4c0), so a
// mastery doesn't win the damage back.

}  // namespace pd2r::skills

namespace {

// PD2's debuff on one mastery stat: -(reduction + mastery% of it), the scaled part truncated (0x1037c4c0), so a
// mastery doesn't win the damage back.
constexpr auto TeleportDebuff(int32_t reduction, int32_t mastery) noexcept -> int32_t {
	const double scaled = static_cast<double>(mastery) * reduction / 100.0;
	return -((scaled > 2147483647.0 ? INT32_MAX : static_cast<int32_t>(scaled)) + reduction);
}

static_assert(TeleportDebuff(49, 0) == -49);

static_assert(TeleportDebuff(30, 50) == -45);   // a 50% mastery is lowered by half as much again

static_assert(TeleportDebuff(25, -10) == -23);  // -2.5 truncates to -2

// PD2 srvdo 155: the core, then a ring of calc2 srvmissilea at the new point (LucionBlink's nova), then aurastate
// (teleportdebuff) for auralencalc frames with each mastery lowered by Param2 - calc1 (TeleportDebuff). Named Pd2
// because rva::SrvDoTeleport is D2R's slot number.
// shortcut: an extended list's new timer is not sent to the client (PD2 sends packet 0x83), as for Blood Warp.
// Upgrade if the client's debuff runs out early in play.

// PD2 srvdo 155: the core, then a ring of calc2 srvmissilea at the new point (LucionBlink's nova), then aurastate
// (teleportdebuff) for auralencalc frames with each mastery lowered by Param2 - calc1 (TeleportDebuff). Named Pd2
// because rva::SrvDoTeleport is D2R's slot number.
// shortcut: an extended list's new timer is not sent to the client (PD2 sends packet 0x83), as for Blood Warp.
// Upgrade if the client's debuff runs out early in play.
auto SrvDoPd2Teleport(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr || !TeleportCore(game, unit, skillId, level)) {
		return 0;
	}
	int32_t x = 0, y = 0;
	d2r::UnitXY(unit, &x, &y);
	const int32_t count = d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level);
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA);
	int32_t created = 0;
	if (count > 0 && d2r::ValidMissile(dataFlag, missile)) {
		created = ServerRing(game, unit, missile, x, y, count, skillId, level);
	}
	const int32_t state = At<int16_t>(skill, SkAuraState);
	const int32_t frames = state > 0 ? d2r::EvalCalc(game, unit, skill, SkAuraLenCalc, skillId, level) : 0;
	const int32_t reduction = At<int32_t>(skill, SkParam2) - d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level);
	int32_t mastery[std::size(Masteries)] {}, debuff[std::size(Masteries)] {};
	const char* list = "none";
	if (frames > 0) {  // PD2's state helper refuses a length under 1
		const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
		for (size_t i = 0; i < std::size(Masteries); i++) {
			mastery[i] = unitStat(unit, Masteries[i], 0);
			debuff[i] = TeleportDebuff(reduction, mastery[i]);
		}
		list = TimedStatList(dataFlag, unit, unit, skillId, level, frames, state,
		                     {{Masteries[0], debuff[0]}, {Masteries[1], debuff[1]}, {Masteries[2], debuff[2]},
		                      {Masteries[3], debuff[3]}});
	}
	return 1;
}

// --- Blood Warp (PD2 srvdofunc 157 -> router 190, srvstfunc 66 -> 80, cltstfunc 56 -> 63) --------------------------
// Read from ProjectDiablo.dll: srvdo 157 at 0x103b1aa0, srvst 66 at 0x103b6460, cltst 56 at 0x103acda0
// (docs/research/skills/w12-blood-warp.md). The cast costs life and never kills; the cost comes back as hpregen.

}  // namespace

PD2R_ROUTE(5, Teleport, SrvDoPd2Teleport)
PD2R_ROUTE(6, Blink, SrvDoPd2Teleport)
PD2R_ROUTE(7, LucionBlink, SrvDoPd2Teleport)
PD2R_ROUTE(8, MonBloodwarp, SrvDoPd2Teleport)
