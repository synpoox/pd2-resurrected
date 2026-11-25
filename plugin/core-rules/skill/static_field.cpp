// static_field
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

// --- Static Field (PD2 srvdofunc 160 -> router 190) ------------------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 160 at 0x103b1dc0, per-unit damage 0x10302830, per-unit debuff 0x1036c920
// (docs/research/skills/w27-static-field-combustion.md). Two passes over the units within aurarangecalc that aurafilter
// takes: lightning damage from current life (StaticFieldDamage), then auratargetstate with aurastat1-6 for
// auralencalc frames.
std::atomic<uint32_t> g_staticFields {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t StaticField = 42, MercStaticField = 528, StaticFieldProc = 679;  // merged ids
// Monsters PD2's damage pass skips, as merged monstats ids: uberdiablonew, rathmaBone, rathmaPoison, rathmaBoneClone,
// rathmaPoisonClone, Lucion

// Monsters PD2's damage pass skips, as merged monstats ids: uberdiablonew, rathmaBone, rathmaPoison, rathmaBoneClone,
// rathmaPoisonClone, Lucion
constexpr int32_t NoStaticField[] = {806, 950, 951, 952, 953, 1129};
// Mercenaries (HirelingClasses in skill_functions.h): their resist reductions are never halved

// PD2's a * b / max(c, 1) in doubles, truncated, INT32_MAX past it (0x1037c4c0)

// PD2's a * b / max(c, 1) in doubles, truncated, INT32_MAX past it (0x1037c4c0)
constexpr auto MulDiv(int32_t a, double b, int32_t c) noexcept -> int32_t {
	const double v = static_cast<double>(a) * b / static_cast<double>(c > 1 ? c : 1);
	return v > 2147483647.0 ? INT32_MAX : (v < -2147483648.0 ? INT32_MIN : static_cast<int32_t>(v));
}

// D2Common #11027: current life as a percentage of max life, in whole points

// PD2's damage on one unit, in life's 1/256 (0x10302830): calc1 percent of current life, at least calc2; if that,
// lowered by lightresist, would beat (100 - StaticFieldMin) percent of max life (itself lowered by the resist, never
// raised), that max-life share instead. The resist is applied again when the hit lands.
constexpr auto StaticFieldDamage(int32_t life, int32_t maxLife, int32_t calc1, int32_t calc2, int32_t fieldMin,
                                 int32_t resist) noexcept -> int32_t {
	const int32_t cap = MulDiv(maxLife, static_cast<double>(static_cast<uint32_t>(100 - fieldMin)), 100);
	const int32_t damage = MulDiv(life, calc1, 100);
	const int32_t resisted = MulDiv(damage, 100 - resist, 100);
	const int32_t capResisted = MulDiv(cap, 100 - resist, 100);
	const int32_t limit = capResisted > cap ? cap : capResisted;
	const int32_t out = static_cast<uint32_t>(damage) < static_cast<uint32_t>(calc2) ? calc2 : damage;
	return resisted > limit ? cap : out;
}

static_assert(StaticFieldDamage(1000 << 8, 1000 << 8, 8, 0, 85, 0) == 80 << 8);     // 8% of 1000 life

static_assert(StaticFieldDamage(1000 << 8, 1000 << 8, 25, 0, 85, 0) == 150 << 8);   // capped at 15% of max life

static_assert(StaticFieldDamage(100 << 8, 1000 << 8, 8, 20 << 8, 0, 0) == 20 << 8);  // calc2's minimum wins

static_assert(StaticFieldDamage(1000 << 8, 1000 << 8, 8, 0, 85, 50) == 80 << 8);    // a resist leaves the cast as is

static_assert(StaticFieldDamage(1000 << 8, 1000 << 8, 10, 0, 92, -50) == 80 << 8);  // 150 resisted > 80: the cap

// PD2's 0x1036df10: a resist reduction (damage, magic, fire, light, cold, poison resist) on a monster that is not a
// mercenary and already resists 100 or more is halved.

static_assert(ImmuneHalved(-25, 41, true, 100) == -12);

static_assert(ImmuneHalved(-25, 41, true, 99) == -25);

static_assert(ImmuneHalved(-25, 41, false, 120) == -25);

static_assert(ImmuneHalved(-25, 7, true, 120) == -25);


struct StaticFieldHit {
	int32_t calc1, calc2, fieldMin;
	int32_t hits = 0;
	int32_t life = 0, maxLife = 0, resist = 0, damage = 0;  // the first unit hit, for the log
};


struct StaticFieldDebuff {
	int32_t skillId, level, frames, state;
	StatValue stats[AuraStats] {};
	size_t count = 0;
	int32_t lists = 0, value = 0;  // new lists, and the first one's aurastat1 value, for the log
};

// One unit in range (PD2's 0x10302830), the hit built as D2R's Static Field (0x5556a0) builds it.

// One unit in range (PD2's 0x10302830), the hit built as D2R's Static Field (0x5556a0) builds it.
auto StaticFieldOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	using namespace d2r::rva;
	auto* hit = static_cast<StaticFieldHit*>(area->arg);
	auto* game = static_cast<Game*>(area->game);
	if (victim == nullptr || game == nullptr || area->unit == nullptr ||
	    std::find(std::begin(NoStaticField), std::end(NoStaticField), At<int32_t>(victim, UnitClassId)) !=
	        std::end(NoStaticField)) {
		return 0;
	}
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const int32_t life = unitStat(victim, StatLife, 0), maxLife = unitStat(victim, StatMaxLife, 0);
	if (life < 0x100 || static_cast<uint32_t>(LifePercent(life, maxLife)) <= static_cast<uint32_t>(hit->fieldMin)) {
		return 0;
	}
	const int32_t resist = unitStat(victim, StatLightResist, 0);
	const int32_t amount = StaticFieldDamage(life, maxLife, hit->calc1, hit->calc2, hit->fieldMin, resist);
	d2r::LeapKnockback damage;
	damage.resultFlags = 0x4001;
	constexpr uint32_t Lightning = 2;  // PD2 deals lightning whatever the row's EType
	d2r::Engine<void (*)(Unit*, void*, uint32_t, int32_t, int32_t, void*, void*) noexcept>(AddElementDamage)(
		area->unit, &damage, Lightning, amount, 0, nullptr, nullptr);
	*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&damage) + DmgHitClass) |= 0xd;
	*(reinterpret_cast<uint8_t*>(&damage) + DamageFlag148) = 1;
	d2r::Engine<void (*)(Game*, Unit*, Unit*, int32_t, void*) noexcept>(HitEvents)(game, area->unit, victim, 1, &damage);
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*) noexcept>(DealDamage)(game, area->unit, victim, &damage);
	ReleaseDamage(damage);
	if (hit->hits++ == 0) {
		hit->life = life;
		hit->maxLife = maxLife;
		hit->resist = resist;
		hit->damage = amount;
	}
	return 1;
}

// One unit in range (PD2's 0x1036c920): the debuff list, owned by the caster.
// shortcut: PD2's state helper (0x1036d7d0) also refuses some monsters by monstats flags; NewStateList's own checks
// stand in (docs/deviations.md). Upgrade if a monster PD2 leaves alone shows the debuff in play.

// One unit in range (PD2's 0x1036c920): the debuff list, owned by the caster.
// shortcut: PD2's state helper (0x1036d7d0) also refuses some monsters by monstats flags; NewStateList's own checks
// stand in (docs/deviations.md). Upgrade if a monster PD2 leaves alone shows the debuff in play.
auto StaticFieldDebuffOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	using namespace d2r::rva;
	auto* debuff = static_cast<StaticFieldDebuff*>(area->arg);
	auto* game = static_cast<Game*>(area->game);
	if (victim == nullptr || game == nullptr || debuff->frames < 1) {
		return 0;
	}
	const int32_t classId = At<int32_t>(victim, UnitClassId);
	const bool monster = At<int32_t>(victim, UnitType) != 0 &&
	                     std::find(std::begin(HirelingClasses), std::end(HirelingClasses), classId) == std::end(HirelingClasses);
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	StatValue stats[AuraStats] {};
	for (size_t i = 0; i < debuff->count; i++) {
		const StatValue& s = debuff->stats[i];
		stats[i] = {s.stat, ImmuneHalved(s.value, s.stat, monster, unitStat(victim, s.stat, 0))};
	}
	const char* list = TimedStatList(d2r::DataFlag(game), area->unit, victim, debuff->skillId, debuff->level,
	                                 debuff->frames, debuff->state, stats, stats + debuff->count);
	if (std::strcmp(list, "new") == 0 && debuff->lists++ == 0) {
		debuff->value = stats[0].value;
	}
	return 1;
}

// PD2 srvdo 160: the damage pass, then the debuff pass (with AreaUnits' flag 1, as PD2 passes it). 0 when aurastat1
// is not a stat, after the damage, as PD2.
// D2R's Static Field asks 0x432c40 (an online cast limit) first; PD2 has none, so neither does this.

// PD2 srvdo 160: the damage pass, then the debuff pass (with AreaUnits' flag 1, as PD2 passes it). 0 when aurastat1
// is not a stat, after the damage, as PD2.
// D2R's Static Field asks 0x432c40 (an online cast limit) first; PD2 has none, so neither does this.
auto SrvDoStaticField(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	const auto* difficulty = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(DifficultyRecord)(
		dataFlag, At<uint8_t>(game, GameDifficulty));
	if (skill == nullptr || difficulty == nullptr) {
		return 0;
	}
	const auto calc = [&](int field) { return d2r::EvalCalc(game, unit, skill, field, skillId, level); };
	StaticFieldHit hit {calc(SkCalc1), calc(SkCalc2), At<int32_t>(difficulty, DiffStaticFieldMin)};
	const int32_t radius = calc(SkAuraRangeCalc);
	const auto mask = At<uint32_t>(skill, SkAuraFilter);
	d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit), 0, 0};  // 0, 0: at the caster
	const auto area = d2r::Engine<AreaFn>(AreaUnits);
	area(game, unit, &center, radius, mask, StaticFieldOne, &hit, 0);

	StaticFieldDebuff debuff {skillId, level, calc(SkAuraLenCalc), At<int16_t>(skill, SkAuraTargetState)};
	for (int i = 0; i < static_cast<int>(AuraStats); i++) {
		const int32_t stat = At<int16_t>(skill, SkAuraStat1 + i * 2);
		if (stat < 0) {
			if (i == 0) {
				return 0;
			}
			continue;
		}
		debuff.stats[debuff.count++] = {stat, calc(SkAuraStatCalc1 + i * 4)};
	}
	area(game, unit, &center, radius, mask, StaticFieldDebuffOne, &debuff, 1);
	++g_staticFields;
	return 1;
}


}  // namespace

PD2R_ROUTE(26, StaticField, SrvDoStaticField)
PD2R_ROUTE(27, MercStaticField, SrvDoStaticField)
PD2R_ROUTE(28, StaticFieldProc, SrvDoStaticField)
