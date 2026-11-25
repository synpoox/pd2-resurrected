// blood_warp
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

// --- Blood Warp (PD2 srvdofunc 157 -> router 190, srvstfunc 66 -> 80, cltstfunc 56 -> 63) --------------------------
// Read from ProjectDiablo.dll: srvdo 157 at 0x103b1aa0, srvst 66 at 0x103b6460, cltst 56 at 0x103acda0
// (docs/research/skills/w12-blood-warp.md). The cast costs life and never kills; the cost comes back as hpregen.
std::atomic<uint32_t> g_bloodWarps {0};

std::atomic<uint32_t> g_bloodWarpsRefused {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t BloodWarp = 439;  // merged id (PD2 367)

// PD2's cost, in life's 1/256: calc1 percent of current life, at least calc2. PD2 converts calc1 as unsigned and
// truncates with cvttsd2si, so a negative calc1 (a life reduction over 12) overflows to INT32_MIN and calc2 wins.

// PD2's cost, in life's 1/256: calc1 percent of current life, at least calc2. PD2 converts calc1 as unsigned and
// truncates with cvttsd2si, so a negative calc1 (a life reduction over 12) overflows to INT32_MIN and calc2 wins.
constexpr auto BloodWarpCost(int32_t calc1, int32_t calc2, int32_t life) noexcept -> int32_t {
	const double v = static_cast<double>(static_cast<uint32_t>(calc1)) / 100.0 * life;
	const int32_t cost = v > -2147483649.0 && v < 2147483648.0 ? static_cast<int32_t>(v) : INT32_MIN;
	return cost > calc2 ? cost : calc2;
}

static_assert(BloodWarpCost(12, 512, 100 << 8) == 3072);     // 12% of 100 life beats the 2-life minimum

static_assert(BloodWarpCost(12, 15360, 100 << 8) == 15360);  // calc2's 60-life cap wins

static_assert(BloodWarpCost(-1, 512, 100 << 8) == 512);      // the overflow leaves the minimum


struct LifeCost {
	const uint8_t* skill = nullptr;  // the skill record; null refuses
	int32_t cost = 0, life = 0;
	auto Payable() const noexcept -> bool { return skill != nullptr && cost < life; }  // the cast never kills
};


auto BloodWarpLife(uint32_t dataFlag, Unit* unit, int32_t skillId, int32_t level) noexcept -> LifeCost {
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return {};
	}
	const auto calc = [&](int field) {
		return d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, field), skillId, level);
	};
	const int32_t life = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(
		unit, d2r::rva::StatLife, 0);
	return {skill, BloodWarpCost(calc(d2r::rva::SkCalc1), calc(d2r::rva::SkCalc2), life), life};
}

// The server's check (start and do): counts the refusals.

// The server's check (start and do): counts the refusals.
auto ServerPayable(const LifeCost& c) noexcept -> bool {
	if (!c.Payable()) {
		g_bloodWarpsRefused.fetch_add(1);
		return false;
	}
	return true;
}

// Start (PD2 srvstfunc 66, here 80): refuse a cast the caster can't pay for.

// Start (PD2 srvstfunc 66, here 80): refuse a cast the caster can't pay for.
auto SrvStBloodWarp(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	return ServerPayable(BloodWarpLife(d2r::DataFlag(game), unit, skillId, level)) ? 1 : 0;
}

// Client half (PD2 cltstfunc 56, here 63): the same check on the client's copy of the caster.

// Client half (PD2 cltstfunc 56, here 63): the same check on the client's copy of the caster.
auto CltStBloodWarp(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	if (unit == nullptr) {
		return 0;
	}
	return BloodWarpLife(d2r::api.unitDataFlag(unit), unit, skillId, level).Payable() ? 1 : 0;
}

// PD2 srvdofunc 157: teleport, then pay the cost (checked again at the new point) and get it back over auralencalc
// frames as aurastat1 (hpregen) = cost / frames + 1 on an aurastate (bloodwarp) list. A recast at the same level
// extends the running list and keeps its regen; a lower level pays and adds none (TimedStatList).
// shortcut: an extended list's new timer is not sent to the client (PD2 sends packet 0x83). Upgrade if the client's
// state runs out early in play.

// PD2 srvdofunc 157: teleport, then pay the cost (checked again at the new point) and get it back over auralencalc
// frames as aurastat1 (hpregen) = cost / frames + 1 on an aurastate (bloodwarp) list. A recast at the same level
// extends the running list and keeps its regen; a lower level pays and adds none (TimedStatList).
// shortcut: an extended list's new timer is not sent to the client (PD2 sends packet 0x83). Upgrade if the client's
// state runs out early in play.
auto SrvDoBloodWarp(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	if (!TeleportCore(game, unit, skillId, level)) {
		return 0;
	}
	const LifeCost c = BloodWarpLife(dataFlag, unit, skillId, level);  // again, at the new point
	if (!ServerPayable(c)) {
		return 0;
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(unit, StatLife, c.life - c.cost, 0);
	const int32_t frames = d2r::EvalCalc(game, unit, c.skill, SkAuraLenCalc, skillId, level);
	const int32_t state = At<int16_t>(c.skill, SkAuraState);
	const int32_t stat = At<int16_t>(c.skill, SkAuraStat1);
	int32_t regen = 0;
	const char* list = "none";
	if (frames > 0 && state > 0 && stat >= 0) {  // PD2 divides by frames unchecked
		regen = c.cost / frames + 1;
		list = TimedStatList(dataFlag, unit, unit, skillId, level, frames, state, {{stat, regen}});
	}
	++g_bloodWarps;
	return 1;
}

// --- Desecrate (PD2 srvdofunc 161 -> router 190, cltdofunc 101 -> 121) -------------------------------------------
// Read from ProjectDiablo.dll: srvdo 161 at 0x103b1fa0, cltdo 101 at 0x103ab630 (docs/research/skills/necromancer-w13.md).

}  // namespace

PD2R_SLOT(6, SrvSt, 80, static_cast<SrvDoFn>(SrvStBloodWarp), "blood warp")
PD2R_SLOT(7, CltSt, 63, static_cast<CltDoFn>(CltStBloodWarp), "blood warp")
PD2R_ROUTE(4, BloodWarp, SrvDoBloodWarp)
