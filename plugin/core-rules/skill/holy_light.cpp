// holy_light
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

// --- Holy Light (PD2 srvdofunc 176) ---------------------------------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 176 at 0x103b3cc0, its per-unit callback 0x103043b0
// (docs/research/skills/w17-holy-light.md). Every unit within aurarangecalc of the target, the caster aside: an ally heals
// cltcalc2-cltcalc3 life, an enemy takes the skill's magic damage; whichever side the target is not on gets half.
std::atomic<uint32_t> g_holyLights {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t HealOverlay = 26;  // PD2's callback puts overlay 26 on each unit it heals


struct HolyLight {
	Unit* target;
	int32_t skillId, level;
	int32_t healMin, healMax;  // life in 1/256
	bool halfHeal, halfDamage;
	int64_t healed, hit;
};

// 1.13c's D2Game 0xe0a60, which PD2 asks: alignment (D2Common #10666) decides, the same when both are hostile (0) or
// both allies (2); a neutral unit (1) is hostile to everyone.
// 0xe0a60 first swaps pets and missiles for their owners; a monster a player owns counts as a player here.
// shortcut: two players (or their pets) count as allies, where 0xe0a60 asks the PvP state. Upgrade when PvP is ported
// (later release).

// 1.13c's D2Game 0xe0a60, which PD2 asks: alignment (D2Common #10666) decides, the same when both are hostile (0) or
// both allies (2); a neutral unit (1) is hostile to everyone.
// 0xe0a60 first swaps pets and missiles for their owners; a monster a player owns counts as a player here.
// shortcut: two players (or their pets) count as allies, where 0xe0a60 asks the PvP state. Upgrade when PvP is ported
// (later release).
auto Hostile(Unit* a, Unit* b) noexcept -> bool {
	using namespace d2r::rva;
	const auto playerSide = [](Unit* u) {
		const int32_t type = At<int32_t>(u, UnitType);
		return type == 0 || (type == UnitMonster && OwnedByPlayer(u));
	};
	if (a == b || (playerSide(a) && playerSide(b))) {
		return false;
	}
	const auto align = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitAlignment);
	const int32_t x = align(a), y = align(b);
	return !(x == y && x != 1);
}


}  // namespace

namespace pd2r::skills {

void PutOverlay(Unit* unit, int32_t overlay) noexcept {
	using namespace d2r::rva;
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(d2r::api.unitDataFlag(unit));
	if (overlay >= 0 && tables != nullptr && overlay < At<int64_t>(tables, TablesOverlayCount)) {
		d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetOverlay)(unit, overlay, 0);
	}
}

// One unit in range (PD2's 0x103043b0).

}  // namespace pd2r::skills

namespace {

// One unit in range (PD2's 0x103043b0).
auto HolyLightOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	using namespace d2r::rva;
	auto* hl = static_cast<HolyLight*>(area->arg);
	Unit* unit = area->unit;
	if (victim == unit) {
		return 0;
	}
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	if (!Hostile(victim, unit)) {
		const int32_t spread = hl->healMax - hl->healMin;
		int32_t heal = hl->healMin + (spread > 0 ? static_cast<int32_t>(d2r::RandomBelow(seed, spread)) : 0);
		heal = std::max(hl->halfHeal ? heal / 2 : heal, 0);
		// 1.13c D2Game 0x4df20: life + heal, at most max life
		const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
		const int32_t life = unitStat(victim, StatLife, 0), maxLife = unitStat(victim, StatMaxLife, 0);
		if (heal > 0) {
			d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(
				victim, StatLife, std::min(life + heal, std::max(maxLife, life)), 0);
		}
		PutOverlay(victim, HealOverlay);
		++hl->healed;
		return 1;
	}
	d2r::LeapKnockback damage;
	damage.resultFlags = ResultHit;
	if (At<int32_t>(victim, UnitType) == UnitMonster && victim != hl->target) {
		const uint8_t* mon = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
			d2r::api.unitDataFlag(victim), At<int32_t>(victim, UnitClassId));
		if (mon != nullptr && (At<uint32_t>(mon, MonFlags) & MonUndead) != 0) {
			damage.resultFlags |= ResultKnockback;
		}
	}
	// PD2 halves the bounds and rolls between them; this halves the roll (the same up to 1/256 of a life point)
	d2r::Engine<void (*)(Unit*, void*, int32_t, int32_t) noexcept>(ElementalDamage)(unit, &damage, hl->skillId, hl->level);
	auto& magic = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&damage) + DmgMagic);
	if (hl->halfDamage) {
		magic /= 2;
	}
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*) noexcept>(AreaApply)(static_cast<Game*>(area->game), unit, victim,
	                                                                     &damage);
	++hl->hit;
	ReleaseDamage(damage);
	return 1;
}

// PD2 srvdofunc 176.
// shortcut: the cast centres on D2R's skill target; PD2 refreshes the caster's path target (1.13c D2Game 0xe07a0) and
// reads that. Upgrade if a cast in game lands on a different unit (docs/deviations.md).

// PD2 srvdofunc 176.
// shortcut: the cast centres on D2R's skill target; PD2 refreshes the caster's path target (1.13c D2Game 0xe07a0) and
// reads that. Upgrade if a cast in game lands on a different unit (docs/deviations.md).
auto SrvDoHolyLight(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr) {
		return 0;
	}
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	int32_t x = 0, y = 0;
	if (target == nullptr || !d2r::UnitXY(target, &x, &y)) {
		return 1;
	}
	const bool enemyTarget = Hostile(target, unit);
	HolyLight hl {target, skillId, level,
	              d2r::EvalCalc(game, unit, skill, SkCltCalc2, skillId, level) << 8,
	              d2r::EvalCalc(game, unit, skill, SkCltCalc3, skillId, level) << 8,
	              enemyTarget, !enemyTarget, 0, 0};
	const int32_t radius = d2r::EvalCalc(game, unit, skill, SkAuraRangeCalc, skillId, level);
	if (radius > 0) {
		d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(target), x, y};
		d2r::Engine<AreaFn>(AreaUnits)(game, unit, &center, radius, At<uint32_t>(skill, SkAuraFilter), HolyLightOne, &hl,
		                               0);
	}
	PutOverlay(target, At<int16_t>(skill, SkSrvOverlay));
	++g_holyLights;
	return 1;
}

// --- Vengeance (PD2 srvdofunc 174) and Joust (PD2 srvdofunc 162 -> router 190) ------------------------------------
// Read from ProjectDiablo.dll: srvdo 174 at 0x103b3570 with its per-unit callback 0x103041f0 and chain hit 0x10303cd0,
// srvdo 162 at 0x103b21e0 (docs/research/skills/w18-vengeance-joust.md).

}  // namespace

PD2R_SLOT(28, SrvDo, 176, static_cast<SrvDoFn>(SrvDoHolyLight), "holy light")
