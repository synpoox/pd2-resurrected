// royal_fire
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

// --- Royal Fire (PD2 srvdofunc 143 -> router 190, cltdofunc 83 -> 123) -----------------------------------------------
// Read from ProjectDiablo.dll: srvdo 143 at 0x103b0df0, cltdo 83 at 0x103ab070; for other skills both fall back to
// vanilla 143 / 83, which D2R keeps in those slots. A ring of srvmissilea (cltmissilea) at every calc1-th of D2's 64
// directions (1.13c D2Game 0x93d60, D2Client 0x40f00, flags 3), from the caster, or for LucionControl from a spot in
// Lucion's arena drawn from its cast sequence (0x103805f0 at unit +0xcc), which both sides step once per cast.
// shortcut: each side keeps that sequence by unit id, not on the unit, and never prunes it (one entry per fight)
constexpr int32_t RadamentBossRoyalFire = 637, LucionRoyalFire = 643;  // merged ids

constexpr int32_t LucionControl = 1130;                                // PD2 1113 (0x459), merged id

// PD2 0x10380670: one step of a cast sequence, the low 20 bits a hash, the top 12 the step count

// PD2 0x103805f0: CastStep, except that the first cast seeds the sequence from the unit id
constexpr auto NextCastSeed(uint32_t v, uint32_t unitType, uint32_t unitId) noexcept -> uint32_t {
	return v == 0 ? ((unitId & 0xfffffff) ^ (unitType << 28)) & 0xfffff : CastStep(v);
}

static_assert(NextCastSeed(0, 1, 0x12345678) == 0x45678 && NextCastSeed(0x45678, 1, 0) >> 20 == 1);

// The controller's ring centre (x, y) in the arena; other casters cast from where they stand. The ring lies in the
// 40 x 40 square around the arena's `centre` spot (#962, arenaspots.txt), so it follows the arena wherever Cartographer
// builds it. No arena means no ring: nothing is invented.

// The controller's ring centre (x, y) in the arena; other casters cast from where they stand. The ring lies in the
// 40 x 40 square around the arena's `centre` spot (#962, arenaspots.txt), so it follows the arena wherever Cartographer
// builds it. No arena means no ring: nothing is invented.
auto RoyalFireCentre(std::map<uint32_t, uint32_t>& seeds, std::mutex& lock, Unit* unit, int32_t* x, int32_t* y)
	noexcept -> bool {
	using namespace d2r::rva;
	if (At<int32_t>(unit, UnitType) != UnitMonster || At<int32_t>(unit, UnitClassId) != LucionControl) {
		return d2r::UnitXY(unit, x, y);
	}
	placements::Spot c{};
	if (!ArenaSpotOf("lucion", "centre", &c)) {
		return false;
	}
	const uint32_t id = At<uint32_t>(unit, UnitIdField);
	uint32_t seed = 0;
	{
		std::lock_guard guard(lock);
		seed = seeds[id] = NextCastSeed(seeds[id], UnitMonster, id);
	}
	*x = static_cast<int32_t>(c.x - 0x14 + (seed + 0xd) % 0x28);
	*y = static_cast<int32_t>(c.y - 0x14 + (seed + 0x13) % 0x28);
	return true;
}


std::map<uint32_t, uint32_t> g_royalSeeds, g_royalClientSeeds;

std::mutex g_royalSeedLock, g_royalClientSeedLock;


auto SrvDoRoyalFire(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA);
	int32_t x = 0, y = 0;
	if (!d2r::ValidMissile(dataFlag, missile) || !RoyalFireCentre(g_royalSeeds, g_royalSeedLock, unit, &x, &y)) {
		return 0;
	}
	const int32_t step = d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level);
	if (step <= 0) {
		return 0;  // PD2 would loop forever
	}
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, missile, x, y, skillId, level);
	const int32_t created = RingStep(step, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		return d2r::api.createMissile(game, &params) != nullptr;
	});
	return 1;
}


auto CltDoRoyalFire(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, SkCltMissileA);
	int32_t x = 0, y = 0;
	if (!d2r::ValidMissile(dataFlag, missile) ||
	    !RoyalFireCentre(g_royalClientSeeds, g_royalClientSeedLock, unit, &x, &y)) {
		return 0;
	}
	const int32_t step = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, SkCalc1), skillId, level);
	if (step <= 0) {
		return 0;
	}
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, missile, x, y, skillId, level);
	const int32_t created = RingStep(step, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		return d2r::api.clientCreateMissile(&params, 0) != nullptr;
	});
	return 1;
}

// --- Death novas (PD2 srvdofunc 169 -> router 190, cltdofunc 105 -> 125) ---------------------------------------------
// Read from ProjectDiablo.dll: srvdo 169 at 0x103b2bc0, cltdo 105 at 0x103ab880 (#480). Each
// half creates one srvmissilea at the caster (flags 1, no target); the client half reads srvmissilea too, not
// cltmissilea, so NihlathakMapDeath (no srvmissilea) draws nothing, as in PD2.

}  // namespace

PD2R_SLOT(38, CltDo, 123, static_cast<CltDoFn>(CltDoRoyalFire), "royal fire")
PD2R_ROUTE(18, RadamentBossRoyalFire, SrvDoRoyalFire)
PD2R_ROUTE(19, LucionRoyalFire, SrvDoRoyalFire)
