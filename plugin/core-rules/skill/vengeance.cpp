// vengeance
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

// --- Vengeance (PD2 srvdofunc 174) and Joust (PD2 srvdofunc 162 -> router 190) ------------------------------------
// Read from ProjectDiablo.dll: srvdo 174 at 0x103b3570 with its per-unit callback 0x103041f0 and chain hit 0x10303cd0,
// srvdo 162 at 0x103b21e0 (docs/research/skills/w18-vengeance-joust.md).
std::atomic<uint32_t> g_vengeances {0};

std::atomic<uint32_t> g_vengeanceLinks {0};

std::atomic<uint32_t> g_jousts {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t Vengeance = 111, Joust = 450;  // merged ids
// Merged states: the aura lists whose elemental damage Vengeance leaves out (holyfire, holywind, holyshock, enchant,
// coldenchant), its chain marker (justhit) and the state each burst clears (shatter)

constexpr int32_t JustHit = 86, Shatter = 107;

constexpr int32_t CriticalStrike = 337;  // passive_critical_strike, the same id in PD2 and D2R
// shortcut: a chain stops after 64 links, PD2 has no cap (calc4 is at most 95%); bounded so bad data can't chain
// forever. Raise it if a logged chain reaches it in play.

// shortcut: a chain stops after 64 links, PD2 has no cap (calc4 is at most 95%); bounded so bad data can't chain
// forever. Raise it if a logged chain reaches it in play.
constexpr int32_t MaxLinks = 64;

// D2R's Leap Attack hit (0x56b170) without its damage%, skill elemental, knockback or states: the to-hit roll with the
// skill's ToHit, weapon damage at srcDam, applied. Returns whether the roll hit. PD2's hit (1.13c's roll 0x103048a0
// and fill 0x1031b4a0) also applies a miss and marks its block with hit flag 2.

// D2R's Leap Attack hit (0x56b170) without its damage%, skill elemental, knockback or states: the to-hit roll with the
// skill's ToHit, weapon damage at srcDam, applied. Returns whether the roll hit. PD2's hit (1.13c's roll 0x103048a0
// and fill 0x1031b4a0) also applies a miss and marks its block with hit flag 2.
auto MeleeHit(Game* game, Unit* unit, Unit* target, const uint8_t* skill, int32_t skillId, int32_t level,
              uint8_t srcDam) noexcept -> bool {
	using namespace d2r::rva;
	const uint8_t mask = *reinterpret_cast<const uint8_t*>(d2r::imageBase + MeleeRollFlagMask);
	const int32_t toHit = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(SkillToHit)(unit, skillId, level);
	d2r::LeapKnockback damage;
	damage.resultFlags = d2r::Engine<uint16_t (*)(Game*, Unit*, Unit*, int32_t, uint8_t, int32_t) noexcept>(MeleeRoll)(
		game, unit, target, toHit, (At<uint8_t>(skill, SkRollFlags) & mask) != 0 ? 1 : 0, 0);
	const bool hit = (damage.resultFlags & ResultHit) != 0;
	if (hit) {
		damage.hitFlags |= 2;
		*reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&damage) + DmgHitClass) =
			d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitHitClass)(unit);
		d2r::Engine<void (*)(Game*, Unit*, Unit*, void*, uint8_t) noexcept>(QueueHit)(game, unit, target, &damage,
		                                                                                    srcDam);
		d2r::Engine<void (*)(Game*, Unit*, Unit*, int32_t, uint8_t) noexcept>(ApplyMeleeHit)(game, unit, target, 0, 1);
	}
	ReleaseDamage(damage);
	return hit;
}

// 1.13c D2Game 0xdbed0, which PD2 calls for each element: min and max each raised by pct %, then a roll between them on
// the unit's seed; nothing when max is not above 0, never below 0. 1.13c splits the product for large values; this
// takes it in 64 bits.

// 1.13c D2Game 0xdbed0, which PD2 calls for each element: min and max each raised by pct %, then a roll between them on
// the unit's seed; nothing when max is not above 0, never below 0. 1.13c splits the product for large values; this
// takes it in 64 bits.
auto RollPercent(uint32_t* seed, int32_t min, int32_t max, int32_t pct) noexcept -> int32_t {
	if (max <= 0) {
		return 0;
	}
	const auto up = [pct](int32_t v) { return v + static_cast<int32_t>(static_cast<int64_t>(v) * pct / 100); };
	const int32_t lo = up(min), hi = up(max);
	const int32_t v = lo + (hi > lo ? static_cast<int32_t>(d2r::RandomBelow(seed, static_cast<uint32_t>(hi - lo))) : 0);
	return std::max(v, 0);
}

// PD2's 0x1037c4c0 with 100: v * pct / 100, truncated

// PD2's 0x1037c4c0 with 100: v * pct / 100, truncated
constexpr auto Percent(int32_t v, int32_t pct) noexcept -> int32_t {
	return static_cast<int32_t>(static_cast<int64_t>(v) * pct / 100);
}

static_assert(Percent(512, 30) == 153);

static_assert(Percent(-512, 30) == -153);


struct VengeanceDamage {
	int32_t pct[3];   // fire, cold, lightning: calc1-3 + the element's mastery
	int32_t base;     // the skill's EMin-EMax roll (EType none), in 1/256
	int32_t dmg[3];   // fire, cold, lightning, in 1/256
	int32_t coldLength;
};

// One Vengeance burst's damage (srvdo 174 and the chain hit 0x10303cd0 build it alike): per element, the caster's own
// min/max damage stats less what the Holy auras and Enchant lists give, raised by calc + mastery % and rolled; then the
// skill's elemental roll, plus its mastery %, added to each. Cold length ELen, 15 frames less per link.

// One Vengeance burst's damage (srvdo 174 and the chain hit 0x10303cd0 build it alike): per element, the caster's own
// min/max damage stats less what the Holy auras and Enchant lists give, raised by calc + mastery % and rolled; then the
// skill's elemental roll, plus its mastery %, added to each. Cold length ELen, 15 frames less per link.
auto RollVengeance(Game* game, Unit* unit, const uint8_t* skill, int32_t skillId, int32_t level, int32_t link) noexcept
	-> VengeanceDamage {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const auto auraStat = [&](int32_t stat, std::initializer_list<int32_t> states) {
		int32_t sum = 0;
		for (const int32_t state : states) {
			void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, state);
			sum += list != nullptr ? d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue)(
			                             dataFlag, list, stat, 0)
			                       : 0;
		}
		return sum;
	};
	struct Element {
		int32_t minStat, calc, mastery;
		std::initializer_list<int32_t> states;
	};
	// firemindam/maxdam 48/49, coldmindam/maxdam 54/55, lightmindam/maxdam 50/51: the same ids in PD2 and D2R
	const Element elements[3] = {{48, SkCalc1, 329, {HolyFire, Enchant}},
	                             {54, SkCalc2, 331, {HolyWind, ColdEnchant}},
	                             {50, SkCalc3, 330, {HolyShock}}};
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	VengeanceDamage d {};
	int32_t mastery[3];
	for (int i = 0; i < 3; ++i) {
		const Element& e = elements[i];
		mastery[i] = unitStat(unit, e.mastery, 0);
		d.pct[i] = d2r::EvalCalc(game, unit, skill, e.calc, skillId, level) + mastery[i];
		const int32_t min = unitStat(unit, e.minStat, 0) - auraStat(e.minStat, e.states);
		const int32_t max = unitStat(unit, e.minStat + 1, 0) - auraStat(e.minStat + 1, e.states);
		d.dmg[i] = RollPercent(seed, min << 8, max << 8, d.pct[i]);
	}
	d2r::LeapKnockback roll;
	d2r::Engine<void (*)(Unit*, void*, int32_t, int32_t) noexcept>(ElementalDamage)(unit, &roll, skillId, level);
	d.base = At<int32_t>(&roll, DmgPhys);
	ReleaseDamage(roll);
	for (int i = 0; i < 3; ++i) {
		d.dmg[i] += d.base + Percent(d.base, mastery[i]);
	}
	const int32_t length = d2r::Engine<int32_t (*)(uint32_t, Unit*, int32_t, int32_t, int32_t) noexcept>(ElementalLength)(
		dataFlag, unit, skillId, level, 1);
	d.coldLength = link == 0 ? length : std::max(length - 15 * link, 0);
	return d;
}


struct VengeanceBurst {
	Unit* origin;  // never chains on from here (PD2: the caster's path target)
	int32_t skillId, level, link;
	VengeanceDamage damage;
	int32_t chance, delay, reach;  // calc4 %, Param5 frames, Param6 subtiles from the caster
	int64_t hit, linked;
};

// A link waiting on its justhit marker: the marker's expiry (VengeanceLinkDue) runs the next burst around the unit.
// PD2 schedules a game timer instead (0x103164f0, event 7). Keyed by the unit with its id: D2R's expiry callback gets
// (game, unit, state), not the list (SkillStateExpired 0x5551f0 never reads its first argument).
// shortcut: a record whose marker went without expiring (replaced, the unit gone) fires on that unit's next marker
// expiry; upgrade if the log shows links from units no burst marked.

// A link waiting on its justhit marker: the marker's expiry (VengeanceLinkDue) runs the next burst around the unit.
// PD2 schedules a game timer instead (0x103164f0, event 7). Keyed by the unit with its id: D2R's expiry callback gets
// (game, unit, state), not the list (SkillStateExpired 0x5551f0 never reads its first argument).
// shortcut: a record whose marker went without expiring (replaced, the unit gone) fires on that unit's next marker
// expiry; upgrade if the log shows links from units no burst marked.
struct VengeanceLink {
	Unit* unit;
	uint32_t unitId;
	Game* game;
	int32_t casterType;
	uint32_t casterId;
	int32_t skillId, level, link;
};
// shortcut: a full ring overwrites its oldest waiting link (it never fires); upgrade if the log shows lost links.

// shortcut: a full ring overwrites its oldest waiting link (it never fires); upgrade if the log shows lost links.
VengeanceLink g_links[256] {};

size_t g_nextLink = 0;

std::mutex g_linksLock;


auto Burst(Game* game, Unit* caster, Unit* center, Unit* origin, const uint8_t* skill, int32_t skillId, int32_t level,
           int32_t link) noexcept -> VengeanceBurst;


void VengeanceLinkDue(void* game, Unit* unit, int32_t state) noexcept {
	using namespace d2r::rva;
	VengeanceLink due {};
	{
		const std::lock_guard<std::mutex> lock(g_linksLock);
		for (VengeanceLink& l : g_links) {
			if (l.unit == unit) {
				due = l;
				l = {};
			}
		}
	}
	// the burst runs before the marker goes, so the unit can't link to itself.
	// shortcut: the marker lasts Param5 frames, PD2's Param5 + 1 (its timer fires at Param5); upgrade if a unit marked
	// in that last frame links in game where PD2's would not.
	if (due.unit != nullptr && due.unitId == At<uint32_t>(unit, UnitIdField)) {
		Unit* caster = d2r::Engine<Unit* (*)(Game*, int32_t, int32_t) noexcept>(FindUnit)(due.game, due.casterType,
		                                                                                   static_cast<int32_t>(due.casterId));
		const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(due.game), due.skillId);
		void* path = caster != nullptr ? At<void*>(caster, UnitPath) : nullptr;
		if (skill != nullptr && path != nullptr) {
			const VengeanceBurst b = Burst(due.game, caster, unit, At<Unit*>(path, PathTargetUnit), skill, due.skillId,
			                               due.level, due.link);
			++g_vengeanceLinks;
		}
	}
	d2r::Engine<void (*)(void*, Unit*, int32_t) noexcept>(SkillStateExpired)(game, unit, state);
}

// One unit in range (PD2's 0x103041f0): it takes the burst; then, unless already marked, within reach of the caster,
// it is marked justhit for the link delay, and, unless it is the origin, links on at calc4 % (its own seed).

// One unit in range (PD2's 0x103041f0): it takes the burst; then, unless already marked, within reach of the caster,
// it is marked justhit for the link delay, and, unless it is the origin, links on at calc4 % (its own seed).
auto VengeanceOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	using namespace d2r::rva;
	auto* b = static_cast<VengeanceBurst*>(area->arg);
	auto* game = static_cast<Game*>(area->game);
	Unit* caster = area->unit;
	d2r::LeapKnockback damage;
	damage.resultFlags = ResultHit;
	auto* block = reinterpret_cast<uint8_t*>(&damage);
	*reinterpret_cast<int32_t*>(block + DmgFire) = b->damage.dmg[0];
	*reinterpret_cast<int32_t*>(block + DmgCold) = b->damage.dmg[1];
	*reinterpret_cast<int32_t*>(block + DmgLight) = b->damage.dmg[2];
	*reinterpret_cast<int32_t*>(block + DmgColdLength) = b->damage.coldLength;
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*) noexcept>(AreaApply)(game, caster, victim, &damage);
	ReleaseDamage(damage);
	++b->hit;
	int32_t cx = 0, cy = 0, vx = 0, vy = 0;
	if (d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(victim, JustHit) != 0 ||
	    !d2r::UnitXY(caster, &cx, &cy) || !d2r::UnitXY(victim, &vx, &vy)) {
		return 1;
	}
	const int64_t dx = vx - cx, dy = vy - cy;
	if (dx * dx + dy * dy >= static_cast<int64_t>(b->reach) * b->reach) {
		return 1;
	}
	d2r::StateListRequest request {victim, victim, b->skillId, b->level, std::max(b->delay, 1), -1, 0, JustHit,
	                               reinterpret_cast<void (*)()>(VengeanceLinkDue)};
	void* list = d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	if (list == nullptr) {
		return 1;
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateGfxFlag)(victim, JustHit, 1);
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(victim);
	if (victim != b->origin && static_cast<int32_t>(d2r::RandomBelow(seed, 100)) < b->chance && b->link < MaxLinks) {
		const std::lock_guard<std::mutex> lock(g_linksLock);
		g_links[g_nextLink++ % std::size(g_links)] = {victim, At<uint32_t>(victim, UnitIdField), game,
		                                              At<int32_t>(caster, UnitType), At<uint32_t>(caster, UnitIdField),
		                                              b->skillId, b->level, b->link + 1};
		++b->linked;
	}
	return 1;
}

// A burst around `center` (srvdo 174 on its target, link 0; the chain hit on a linked unit): auratargetstate on it for
// Param5 frames, every unit within aurarangecalc (less 1 per link, at least 2) of it takes the damage, and its shatter
// state is cleared.

// A burst around `center` (srvdo 174 on its target, link 0; the chain hit on a linked unit): auratargetstate on it for
// Param5 frames, every unit within aurarangecalc (less 1 per link, at least 2) of it takes the damage, and its shatter
// state is cleared.
auto Burst(Game* game, Unit* caster, Unit* center, Unit* origin, const uint8_t* skill, int32_t skillId, int32_t level,
           int32_t link) noexcept -> VengeanceBurst {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	VengeanceBurst b {origin, skillId, level, link, RollVengeance(game, caster, skill, skillId, level, link),
	                  d2r::EvalCalc(game, caster, skill, SkCalc4, skillId, level), At<int32_t>(skill, SkParam5),
	                  At<int32_t>(skill, SkParam6), 0, 0};
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	const int32_t mark = At<int16_t>(skill, SkAuraTargetState);
	if (mark > 0 && tables != nullptr && static_cast<uint64_t>(mark) < At<uint64_t>(tables, TablesStateCount) &&
	    b.delay > 0) {
		TimedStatList(dataFlag, caster, center, skillId, level, b.delay, mark, {});
	}
	int32_t radius = d2r::EvalCalc(game, caster, skill, SkAuraRangeCalc, skillId, level);
	radius = link == 0 ? radius : std::max(radius - link, 2);
	int32_t x = 0, y = 0;
	if (radius > 0 && d2r::UnitXY(center, &x, &y)) {
		d2r::AreaCenter area {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(center), x, y};
		if (area.room != nullptr) {
			d2r::Engine<AreaFn>(AreaUnits)(game, caster, &area, radius, AreaUnitMask, VengeanceOne, &b, 0);
		}
	}
	// The state bit off, as D2R's 0x4370b0 removes one: SetStateGfxFlag on 0 only cleared the gfx bit (#623)
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateBit)(center, Shatter, 0);
	return b;
}

// PD2 srvdofunc 174: a melee hit at SrcDam 128; when it hits, a burst on the target (link 0).
// shortcut: the hit centres on D2R's skill target; PD2 refreshes and reads the caster's path target, as Holy Light.
// Upgrade if a hit in game lands on a different unit (docs/deviations.md).

// PD2 srvdofunc 174: a melee hit at SrcDam 128; when it hits, a burst on the target (link 0).
// shortcut: the hit centres on D2R's skill target; PD2 refreshes and reads the caster's path target, as Holy Light.
// Upgrade if a hit in game lands on a different unit (docs/deviations.md).
auto SrvDoVengeance(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr) {
		return 0;
	}
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	if (target == nullptr) {
		return 1;
	}
	const bool hit = MeleeHit(game, unit, target, skill, skillId, level, 0x80);
	VengeanceBurst b {};
	if (hit) {
		b = Burst(game, unit, target, target, skill, skillId, level, 0);
	}
	++g_vengeances;
	return 1;
}

// PD2 srvdofunc 162: the teleport core to the target, then D2R's Leap Attack hit (to-hit with the skill's ToHit,
// damage% calc1, SrcDam) with passive_critical_strike raised to min(itself + calc2, 100) for the hit, then aurastate
// (joust_buff) for auralencalc frames with aurastat1 (velocitypercent). PD2 reads the stat's total and sets its base
// (D2Common #10973, #10887), as this does.
// shortcut: the target is D2R's skill target (PD2 reads the caster's path target) and the hit is D2R's, which also
// gives the aurastate on a hit (the list made next extends it); upgrade if a Joust in game lands elsewhere or its buff
// differs from PD2's (docs/deviations.md).

// PD2 srvdofunc 162: the teleport core to the target, then D2R's Leap Attack hit (to-hit with the skill's ToHit,
// damage% calc1, SrcDam) with passive_critical_strike raised to min(itself + calc2, 100) for the hit, then aurastate
// (joust_buff) for auralencalc frames with aurastat1 (velocitypercent). PD2 reads the stat's total and sets its base
// (D2Common #10973, #10887), as this does.
// shortcut: the target is D2R's skill target (PD2 reads the caster's path target) and the hit is D2R's, which also
// gives the aurastate on a hit (the list made next extends it); upgrade if a Joust in game lands elsewhere or its buff
// differs from PD2's (docs/deviations.md).
auto SrvDoJoust(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr || d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit) == nullptr ||
	    !TeleportCore(game, unit, skillId, level)) {
		return 0;
	}
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const auto setStat = d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat);
	const int32_t crit = unitStat(unit, CriticalStrike, 0);
	const int32_t raised = std::min(crit + d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level), 100);
	setStat(unit, CriticalStrike, raised, 0);
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(SkillMeleeHit)(game, unit, skillId, level);
	setStat(unit, CriticalStrike, crit, 0);
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	const int32_t state = At<int16_t>(skill, SkAuraState), stat = At<int16_t>(skill, SkAuraStat1);
	const int32_t frames = d2r::EvalCalc(game, unit, skill, SkAuraLenCalc, skillId, level);
	const int32_t value = d2r::EvalCalc(game, unit, skill, SkAuraStatCalc1, skillId, level);
	const char* list = "none";
	if (state > 0 && tables != nullptr && static_cast<uint64_t>(state) < At<uint64_t>(tables, TablesStateCount) &&
	    frames > 0 && stat >= 0) {
		list = TimedStatList(dataFlag, unit, unit, skillId, level, frames, state, {{stat, value}});
	}
	++g_jousts;
	return 1;
}

// --- Dragon Flight (PD2 srvdofunc 178; docs/research/skills/w31-dragon-flight-psychic-hammer.md) -------------------------
// Read from ProjectDiablo.dll: srvdo 178 at 0x103b4180.

}  // namespace

PD2R_SLOT(29, SrvDo, 174, static_cast<SrvDoFn>(SrvDoVengeance), "vengeance")
PD2R_ROUTE(10, Joust, SrvDoJoust)
