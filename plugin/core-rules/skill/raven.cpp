// raven
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

// --- Raven: calc3 ravens per cast (PD2 srvdofunc 114; #2002) -------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 114 at 0x103b06c0 (PD2.gpr; installed +0x70). Once per cast: the base spot (the
// caster's skill target, else the caster: 1.13c 0x9fa30, as the creator's own default) and calc3. Then calc3 times:
// the shared creator at a jittered spot, flag 4 clear (the spot, else a spot search of 4); a failed raven ends the cast
// (returns 0, the rest are not made). r4 = rand 0..3, r8 = rand 0..7: raven 0 at (x, y - 1 - r4), 1 at
// (x + 1 + r4, y + 1 + r4), 2 at (x - 1 - r4, y + 1 + r4), the rest at (x - 4 + r8, y - 4 + r8). petmax is the
// creator's: a raven over it replaces the type's oldest. Each raven is 1.13c's: level calc2 (min 1), the summon stats.
// D2R's srvdo 114 (0x576f70) is 1.13c's plus the skill1 at the cast level and the caster's target for the ravens, one
// raven at the target. It is wrapped: called calc3 times, its creator call redirected to set the spot. No SDK service
// summons a pet, so the slot and the call are found by signature.
// Hits: PD2's raven think (monai 107, stock 1.13c 0xaa1d0) gives Param5 + (level - 1) x Param6; D2R's (0x5cca50) reads
// calc3 of the raven's skill instead (vanilla's calc3 is that formula), PD2's raven count here. The redirect writes
// PD2's hits into the new raven's AI params (+0x44), which the think reads only while its init's -1 is there.
constexpr int32_t SrvDoRavenSlot = 114;

constexpr const char* SrvDoRavenEntry = "48 89 5c 24 10 48 89 74 24 18 48 89 7c 24 20 55 41 54 41 55 41 56 41 57 48 8b ec "
                                        "48 81 ec 80 00 00 00 48 8b fa 48 8b f1 0f b6 89 06 01 00 00 41 8b d0 45 8b e1 "
                                        "45 8b f8 e8";
// petmax into the params, then the creator (0x4350e0) at 0x5770da

// petmax into the params, then the creator (0x4350e0) at 0x5770da
constexpr const char* RavenSummonCall = "45 8b 86 14 01 00 00 44 89 64 24 20 e8 ?? ?? ?? ?? 48 8d 55 d0 89 45 f8 48 8b ce "
                                        "e8 ?? ?? ?? ?? 48 89 45 c8 48 8b d8 48 85 c0 0f 84";

constexpr uint32_t RavenSummonCallAt = 0x1b, RavenSummonFromEntry = 0x14f;

SrvDoFn g_srvDoRaven = nullptr;

d2r::SummonPetFn g_ravenSummon = nullptr;


struct RavenCast {
	int32_t index, x, y, hits;
	uint32_t* seed;
};

thread_local RavenCast* t_raven = nullptr;  // set while SrvDoRaven runs D2R's do


struct RavenOffset {
	int32_t x, y;
};
// a: r4 for ravens 0-2, r8 after; b: the second roll (unused by raven 0)

// a: r4 for ravens 0-2, r8 after; b: the second roll (unused by raven 0)
constexpr auto RavenJitter(int32_t index, int32_t a, int32_t b) noexcept -> RavenOffset {
	switch (index) {
	case 0: return {0, -1 - a};
	case 1: return {1 + a, 1 + b};
	case 2: return {-1 - a, 1 + b};
	default: return {a - 4, b - 4};
	}
}

static_assert(RavenJitter(0, 3, 9).x == 0 && RavenJitter(0, 3, 9).y == -4);

static_assert(RavenJitter(1, 0, 3).x == 1 && RavenJitter(1, 0, 3).y == 4);

static_assert(RavenJitter(2, 3, 0).x == -4 && RavenJitter(2, 3, 0).y == 1);

static_assert(RavenJitter(5, 0, 7).x == -4 && RavenJitter(5, 0, 7).y == 3);


auto RavenSummon(Game* game, d2r::SummonParams* params) noexcept -> Unit* {
	RavenCast* cast = t_raven;
	if (cast != nullptr && params != nullptr) {
		const uint32_t bound = cast->index < 3 ? 4 : 8;
		const auto a = static_cast<int32_t>(d2r::RandomBelow(cast->seed, bound));
		const auto b = static_cast<int32_t>(d2r::RandomBelow(cast->seed, bound));
		const RavenOffset at = RavenJitter(cast->index, a, b);
		params->flags |= 1;  // at x/y
		params->x = cast->x + at.x;
		params->y = cast->y + at.y;
	}
	Unit* pet = g_ravenSummon(game, params);
	uint8_t* data = pet != nullptr && cast != nullptr ? At<uint8_t*>(pet, d2r::rva::UnitData) : nullptr;
	uint8_t* ai = data != nullptr ? At<uint8_t*>(data, MonDataAi) : nullptr;
	if (ai != nullptr) {
		*reinterpret_cast<int32_t*>(ai + d2r::rva::AiControlParams) = cast->hits;
	}
	return pet;
}


auto SrvDoRaven(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint8_t* skill = game != nullptr && unit != nullptr ? d2r::api.getSkillRecord(d2r::DataFlag(game), skillId)
	                                                          : nullptr;
	RavenCast cast {};
	Unit* target = skill != nullptr ? d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit) : nullptr;
	if (skill == nullptr || !d2r::UnitXY(target != nullptr && target != unit ? target : unit, &cast.x, &cast.y)) {
		return g_srvDoRaven(game, unit, skillId, level);  // D2R's refusals and one raven
	}
	const int32_t count = d2r::EvalCalc(game, unit, skill, SkCalc3, skillId, level);
	cast.hits = level < 1 ? 0 : At<int32_t>(skill, SkParam5) + (level - 1) * At<int32_t>(skill, SkParam6);
	cast.seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	int64_t result = 1;
	t_raven = &cast;
	for (; cast.index < count; ++cast.index) {
		if (g_srvDoRaven(game, unit, skillId, level) == 0) {
			result = 0;
			break;
		}
	}
	t_raven = nullptr;
	return result;
}

// The creator call first: alone it only passes through (no cast is set). False when either is refused.

// The creator call first: alone it only passes through (no cast is set). False when either is refused.
auto InstallRavenCount(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t entry = 0, call = 0;
	if (d2r::sig::SrvDoTable.state != sigscan::State::Ok ||
	    sigscan::ScanUnique(context->exeBase, SrvDoRavenEntry, 0, &entry) != 1 ||
	    sigscan::ScanUnique(context->exeBase, RavenSummonCall, 0, &call) != 1 || call != entry + RavenSummonFromEntry) {
		return false;
	}
	const uint64_t d2rDo = context->exeBase + entry;
	g_srvDoRaven = reinterpret_cast<SrvDoFn>(d2rDo);
	return RedirectCall(context, RavenSummonCall, RavenSummonCallAt, reinterpret_cast<uint64_t>(&RavenSummon),
	                    reinterpret_cast<uint64_t*>(&g_ravenSummon)) &&
	       SwapSlot(context, d2r::sig::SrvDoTable.rva + static_cast<uint64_t>(SrvDoRavenSlot) * 8, d2rDo,
	                reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvDoRaven)));
}

// --- Combustion and the boss novas (PD2 srvdofunc 159 -> router 190, cltdofunc 100 -> 120) ----------------------------
// Read from ProjectDiablo.dll: srvdo 159 at 0x103b1d00, cltdo 100 at 0x103ab580
// (docs/research/skills/w27-static-field-combustion.md). A ring of calc2 missiles of the skill's missile a around the caster.

}  // namespace

auto InstallRaven(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	const bool raven = InstallRavenCount(context);
	plog::Info("raven: pd2 count %s", raven ? "on" : "FAILED (srvdo 114 signature, creator call or slot)");
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; raven count%s", raven ? "" : " FAILED");
	}
	return raven;
}

PD2R_RULE(InstallRaven, 370)

