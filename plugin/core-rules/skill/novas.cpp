// novas
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

// --- Combustion and the boss novas (PD2 srvdofunc 159 -> router 190, cltdofunc 100 -> 120) ----------------------------
// Read from ProjectDiablo.dll: srvdo 159 at 0x103b1d00, cltdo 100 at 0x103ab580
// (docs/research/skills/w27-static-field-combustion.md). A ring of calc2 missiles of the skill's missile a around the caster.
std::atomic<uint32_t> g_novas {0};

std::atomic<uint32_t> g_novaClients {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t Combustion = 448, FingermageBossNova = 472, RathmaTeethNova = 561, RathmaOuterNova = 567,
                  RathmaPacman = 571, LeoricSummonNova = 595, ZharMiniBossCombustion = 617;  // merged ids


}  // namespace

namespace pd2r::skills {

auto SrvDoNova(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA);
	int32_t x = 0, y = 0;
	if (!d2r::ValidMissile(dataFlag, missile) || !d2r::UnitXY(unit, &x, &y)) {
		return 0;
	}
	const int32_t count = d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level);
	const int32_t created = ServerRing(game, unit, missile, x, y, count, skillId, level);
	++g_novas;
	return 1;
}

// Client half (PD2 cltdo 100): the same ring of calc2 cltmissilea.

// Client half (PD2 cltdo 100): the same ring of calc2 cltmissilea.
auto CltDoNova(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	const int32_t missile = At<int16_t>(skill, SkCltMissileA);
	int32_t x = 0, y = 0;
	if (!d2r::ValidMissile(dataFlag, missile) || !d2r::UnitXY(unit, &x, &y)) {
		return 0;
	}
	const int32_t count = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, SkCalc2), skillId, level);
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, missile, x, y, skillId, level);
	const int32_t created = Ring(count, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		return d2r::api.clientCreateMissile(&params, 0) != nullptr;
	});
	++g_novaClients;
	return 1;
}

// --- Item casts of the novas (PD2 ItemEffect 183 -> 187; IR5, #240) ----------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 183 at 0x103b5080 (its installer 0x1036ba20). An item cast of a nova (a hit skill,
// a charge) makes PD2's ring (0x1036d250: 64 of srvmissilea, flags 3, no range) at the skill target unit's position,
// else the caster's. D2R's do dispatcher (0x43acb0) calls ItemEffect's slot for an item cast, instead of srvdofunc.
// approximated: a unit at the centre takes 1 hit per cast in both: the first missile hits and NextHit blocks the rest for
// NextDelay (4) frames, by which time they have passed it. PD2 also sets the ring missiles' flag 0x80000 (+0xc8), which
// PD2's collision (0x103061d0 -> 0x103a9200, set by 0x103a92e0) uses to widen the block's key (owner, missile row) to any
// player. D2R's collision (0x4639a0 -> 0x324a50, set by 0x324820) keys it by the missile's cast id (+0x12c). So two item
// novas of different casts reaching one unit within 4 frames both hit it here; PD2 lets only the first hit.
// Why not D2R's own nova item effect (srvdo 36, 0x56df80), which also rings 64 missiles at the target: it returns 0 without a
// target unit (PD2 falls back to the caster) and gives the missiles a calc range (PD2's ring has none).

// --- Item casts of the novas (PD2 ItemEffect 183 -> 187; IR5, #240) ----------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 183 at 0x103b5080 (its installer 0x1036ba20). An item cast of a nova (a hit skill,
// a charge) makes PD2's ring (0x1036d250: 64 of srvmissilea, flags 3, no range) at the skill target unit's position,
// else the caster's. D2R's do dispatcher (0x43acb0) calls ItemEffect's slot for an item cast, instead of srvdofunc.
// approximated: a unit at the centre takes 1 hit per cast in both: the first missile hits and NextHit blocks the rest for
// NextDelay (4) frames, by which time they have passed it. PD2 also sets the ring missiles' flag 0x80000 (+0xc8), which
// PD2's collision (0x103061d0 -> 0x103a9200, set by 0x103a92e0) uses to widen the block's key (owner, missile row) to any
// player. D2R's collision (0x4639a0 -> 0x324a50, set by 0x324820) keys it by the missile's cast id (+0x12c). So two item
// novas of different casts reaching one unit within 4 frames both hit it here; PD2 lets only the first hit.
// Why not D2R's own nova item effect (srvdo 36, 0x56df80), which also rings 64 missiles at the target: it returns 0 without a
// target unit (PD2 falls back to the caster) and gives the missiles a calc range (PD2's ring has none).
std::atomic<uint32_t> g_itemNovas {0};


}  // namespace pd2r::skills

namespace {

auto SrvDoItemNova(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA);
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	int32_t x = 0, y = 0;
	if (!(target != nullptr && d2r::UnitXY(target, &x, &y)) && !d2r::UnitXY(unit, &x, &y)) {
		return 1;
	}
	const int32_t created = d2r::ValidMissile(dataFlag, missile) ? ServerRing(game, unit, missile, x, y, 64, skillId, level) : 0;
	++g_itemNovas;
	return 1;
}

// --- Royal Fire (PD2 srvdofunc 143 -> router 190, cltdofunc 83 -> 123) -----------------------------------------------
// Read from ProjectDiablo.dll: srvdo 143 at 0x103b0df0, cltdo 83 at 0x103ab070; for other skills both fall back to
// vanilla 143 / 83, which D2R keeps in those slots. A ring of srvmissilea (cltmissilea) at every calc1-th of D2's 64
// directions (1.13c D2Game 0x93d60, D2Client 0x40f00, flags 3), from the caster, or for LucionControl from a spot in
// Lucion's arena drawn from its cast sequence (0x103805f0 at unit +0xcc), which both sides step once per cast.
// shortcut: each side keeps that sequence by unit id, not on the unit, and never prunes it (one entry per fight)

}  // namespace

PD2R_SLOT(36, CltDo, 120, static_cast<CltDoFn>(CltDoNova), "nova")
PD2R_SLOT(37, SrvDo, 187, static_cast<SrvDoFn>(SrvDoItemNova), "item nova")
PD2R_ROUTE(11, Combustion, SrvDoNova)
PD2R_ROUTE(12, FingermageBossNova, SrvDoNova)
PD2R_ROUTE(13, RathmaTeethNova, SrvDoNova)
PD2R_ROUTE(14, RathmaOuterNova, SrvDoNova)
PD2R_ROUTE(15, RathmaPacman, SrvDoNova)
PD2R_ROUTE(16, LeoricSummonNova, SrvDoNova)
PD2R_ROUTE(17, ZharMiniBossCombustion, SrvDoNova)
