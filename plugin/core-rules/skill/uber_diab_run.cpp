// uber_diab_run
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

auto SrvStUberDiabRun(Game* game, Unit* unit, int32_t, int32_t) noexcept -> int64_t {
	using namespace d2r::rva;
	UnitSkill* uskill = game != nullptr && unit != nullptr ? CurrentUnitSkill(unit) : nullptr;
	if (uskill == nullptr) {
		return 0;
	}
	Flags(uskill) = 0;  // PD2 0x1030aec0(skill, 0), as D2R's srvst 54
	auto* path = At<uint8_t*>(unit, UnitPath);
	Unit* target = path != nullptr ? At<Unit*>(path, PathTargetUnit) : nullptr;
	int32_t x = 0, y = 0;
	if (target != nullptr && target != unit && d2r::UnitXY(target, &x, &y)) {
		auto* skillTarget = reinterpret_cast<int32_t*>(uskill + USkillTargetType);
		skillTarget[0] = At<int32_t>(target, UnitType);
		skillTarget[1] = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitId)(target, __FILE__, __LINE__);
		// clears the path's target unit too, as PD2's 0x10309470(path, 0) before 0x103331e0
		d2r::Engine<void (*)(void*, int32_t, int32_t) noexcept>(PathSetTargetXY)(path, x, y);
	} else if (path != nullptr) {
		x = At<uint16_t>(path, 0x10);  // the point already set (PathSetTargetXY)
		y = At<uint16_t>(path, 0x12);
	}
	return 1;
}

// --- Lucion's kit (PD2 srvdofunc 184 Nova, 185 Dash, 186 Punish -> router 190; Dash's srvstfunc 76 -> 40) ------------
// Read from ProjectDiablo.dll: srvdo 184 at 0x103b5110, 185 at 0x103b52b0, 186 at 0x103b56d0, srvst 76 at 0x103b6aa0
// (docs/pd2/uber-bosses-dll.md, section 5). PD2 reads Lucion's AI params and the fight's sub-step (game
// +0x262b); core-events' lucion_ai.cpp keeps them in D2R's per-game counters, read here by the same keys.

}  // namespace

PD2R_SLOT(62, SrvSt, 71, static_cast<SrvDoFn>(SrvStUberDiabRun), "uberdiabrun start")
