// melee_splash
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

// --- Melee splash (PD2 srvdofunc 163 -> router 190) ---------------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 163 at 0x103b2340, its per-unit callback 0x10303190; Revive's aura event 32
// (0x10347370) calls the do function itself (docs/research/skills/w33-melee-splash.md). Every unit within calc3 of the struck
// unit, the struck unit aside, takes a hit with no to-hit roll: a player's current skill's damage (W35) and the
// attacker's weapon damage at that skill's SrcDam, with half the life leech, through D2R's area search with PD2's unit
// mask. The radius is calc3 + the inc_splash_radius held by the weapon's item types / 20, 1 less on a normal-tier base.
// shortcut: Smite's splash takes Smite's skill damage, not its shield damage and state 224 list (0x103060c0), and the
// radius counts only the weapon's own item types, not PD2's off-hand rules for 1han / 2han (0x10381720); upgrade with
// the deviation row. PD2's room check (0x1037c820) refuses only PD2's PvP maps, cut.
std::atomic<uint32_t> g_splashes {0};
// PD2 skips Revive's event 32 and item event functions 14 (item_freeze) and 20 (skill on hit) while a splash hit fires
// its events (the handler set 0x105dc628, read by 0x10347510), so a splash never splashes again or procs on the splashed
// units. Its skill filter (0x105dc5c8, proc_SplashDamage) on events 21 and 30 only refuses a splash inside a splash,
// which Splash does.

}  // namespace

namespace pd2r::skills {

// PD2 skips Revive's event 32 and item event functions 14 (item_freeze) and 20 (skill on hit) while a splash hit fires
// its events (the handler set 0x105dc628, read by 0x10347510), so a splash never splashes again or procs on the splashed
// units. Its skill filter (0x105dc5c8, proc_SplashDamage) on events 21 and 30 only refuses a splash inside a splash,
// which Splash does.
thread_local bool t_splashing = false;

}  // namespace pd2r::skills

namespace {

// PD2's damage% calc for the current skill (0x10305eb0): calc1 for these, calc2 for SplashCalc2; others none.
// Merged ids: Joust 450, Blade Dance 452 (PD2 378, 380).
constexpr int32_t SplashCalc1[] = {0, 10, 96, 97, 107, 126, 133, 144, 147, 151, 152, 232, 260, 450, 452};

constexpr int32_t SplashCalc2[] = {30, 106, 248, 255, 265, 275};

constexpr int32_t SplashKicks[] = {255, 270, 275};  // Dragon Talon, Tail, Flight: the queued kick's damage

constexpr int32_t SplashConcentrate = 144;


constexpr auto Among(int32_t id, std::span<const int32_t> ids) noexcept -> bool {
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

static_assert(Among(147, SplashCalc1) && Among(255, SplashCalc2) && !Among(270, SplashCalc2));


// D2R's to-hit roll (attacker, defender, bonus %, 0) -> non-zero on a hit: 1.13c's and PD2's 0x10305140, with no reach
// test. MeleeRoll calls it after its melee reach test (UnitInReach), which an owner far from its sentinel fails.
constexpr int32_t StatToHitPercentRoll = 119;  // item_tohit_percent, the same row in PD2's and our itemstatcost

}  // namespace

namespace pd2r::skills {

ToHitRollFn g_toHitRoll = nullptr;

}  // namespace pd2r::skills

namespace {

// D2R's area apply (0x436e90) with a melee hit's events: PD2's apply (0x10305880, its event step 0x10305c30) fires
// domeleedamage on the attacker and event 1 on the victim, where D2R's area apply fires 6 and 2.
void DealSplash(Game* game, Unit* attacker, Unit* victim, d2r::LeapKnockback& damage) noexcept {
	using namespace d2r::rva;
	using HitFn = void (*)(Game*, Unit*, Unit*, void*) noexcept;
	if ((damage.resultFlags & ResultHit) == 0) {
		return;
	}
	const uint32_t avoided =
		d2r::Engine<uint32_t (*)(Game*, Unit*, Unit*, int32_t, int32_t) noexcept>(AvoidRoll)(game, attacker, victim, 1, 0);
	if ((avoided & 2) != 0) {
		damage.resultFlags |= 0x100;
	}
	if ((avoided & 4) != 0) {
		damage.resultFlags |= 0x80;
	}
	if ((avoided & 0x10) != 0) {
		damage.resultFlags |= 0x8000;
	}
	if ((avoided & 1) != 0) {
		damage.resultFlags |= 0x10;
	}
	if (avoided != 0) {
		damage.resultFlags &= ~uint32_t {ResultHit};
	}
	if ((damage.resultFlags & ResultHit) != 0 &&
	    d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(victim, 0x36) == 0) {
		damage.resultFlags |= 4;
	}
	d2r::Engine<void (*)(Unit*, Unit*, void*) noexcept>(DamageStep)(attacker, victim, &damage);
	if ((damage.resultFlags & ResultHit) != 0) {
		d2r::Engine<HitFn>(DamageEffects)(game, attacker, victim, &damage);  // what HitEvents runs for an area hit
		d2r::Engine<void (*)(Game*, Unit*, Unit*, int32_t, void*) noexcept>(HitEvents)(game, attacker, victim, 0, &damage);
	}
	d2r::Engine<HitFn>(DealDamage)(game, attacker, victim, &damage);
}

// The queued kick's damage (PD2 copies the attacker's cached hit, 1.13c's 0x70 bytes): the block's plain fields, not its
// list.

// The queued kick's damage (PD2 copies the attacker's cached hit, 1.13c's 0x70 bytes): the block's plain fields, not its
// list.
void CopyKick(d2r::LeapKnockback& damage, const uint8_t* kick) noexcept {
	auto* to = reinterpret_cast<uint8_t*>(&damage);
	std::memcpy(to, kick, offsetof(d2r::LeapKnockback, listData));
	std::memcpy(to + 0x118, kick + 0x118, offsetof(d2r::LeapKnockback, tagged) - 0x118);
}

// The current skill's physical damage, as 1.13c's D2Game 0x9f1a0 adds it: min + rand(max - min) on the attacker's seed

// The current skill's physical damage, as 1.13c's D2Game 0x9f1a0 adds it: min + rand(max - min) on the attacker's seed
void AddSkillPhys(Game* game, Unit* unit, uint8_t* damage, int32_t skillId, int32_t level) noexcept {
	using namespace d2r::rva;
	using PhysFn = int32_t (*)(uint32_t, Unit*, int32_t, int32_t, int32_t) noexcept;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const int32_t min = d2r::Engine<PhysFn>(SkillPhysMin)(dataFlag, unit, skillId, level, 0);
	const int32_t max = d2r::Engine<PhysFn>(SkillPhysMax)(dataFlag, unit, skillId, level, 0);
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	const int32_t roll = max > min ? static_cast<int32_t>(d2r::RandomBelow(seed, static_cast<uint32_t>(max - min))) : 0;
	*reinterpret_cast<int32_t*>(damage + DmgPhys) += min + roll;
}

// One unit in range (PD2's callback 0x10303190): a hit with no to-hit roll and the current skill's flags, damage% and
// hit class, the skill's elemental and physical damage, weapon damage at SrcDam, life leech halved (at least 1). Counts
// the units the hit lands on. Blade Sentinel's block (SentinelHit) sets the callback's other fields: another attacker,
// a to-hit roll, full leech and a damage percent.
// shortcut: PD2 also writes the skill id to a player victim's data (+0x194 of 1.13c's), not decoded; upgrade with
// the deviation row.

}  // namespace

namespace pd2r::skills {

// One unit in range (PD2's callback 0x10303190): a hit with no to-hit roll and the current skill's flags, damage% and
// hit class, the skill's elemental and physical damage, weapon damage at SrcDam, life leech halved (at least 1). Counts
// the units the hit lands on. Blade Sentinel's block (SentinelHit) sets the callback's other fields: another attacker,
// a to-hit roll, full leech and a damage percent.
// shortcut: PD2 also writes the skill id to a player victim's data (+0x194 of 1.13c's), not decoded; upgrade with
// the deviation row.
auto SplashOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	using namespace d2r::rva;
	auto* hit = static_cast<SplashHit*>(area->arg);
	if (victim == hit->struck) {
		return 0;
	}
	auto* game = static_cast<Game*>(area->game);
	Unit* attacker = hit->attacker != nullptr ? hit->attacker : area->unit;
	d2r::LeapKnockback damage;
	auto* block = reinterpret_cast<uint8_t*>(&damage);
	uint16_t rolled = ResultHit;
	if (hit->roll) {  // PD2's callback: roll(attacker, victim, attacker's item_tohit_percent + the block's to-hit, 0)
		const int32_t bonus =
			d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(attacker, StatToHitPercentRoll, 0) + hit->toHit;
		rolled = g_toHitRoll(attacker, victim, bonus, 0) != 0 ? ResultHit : 0;
	}
	++hit->inRange;
	damage.resultFlags = rolled | hit->resultFlags;
	damage.hitFlags = hit->hitFlags;
	*reinterpret_cast<int32_t*>(block + DmgPercent) = hit->percent;
	*reinterpret_cast<int32_t*>(block + DmgHitClass) = hit->hitClass;
	const auto* kick = Among(hit->skillId, SplashKicks)
	                       ? d2r::Engine<const uint8_t* (*)(Unit*, Unit*) noexcept>(QueuedHit)(attacker, hit->struck)
	                       : nullptr;
	if (kick != nullptr) {
		CopyKick(damage, kick);
	} else {
		damage.hitFlags &= ~2u;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), hit->skillId);
	if (hit->skillId == SplashConcentrate && skill != nullptr && At<uint8_t>(skill, SkEType) != 0) {
		block[DmgConvType] = At<uint8_t>(skill, SkEType);
		*reinterpret_cast<int32_t*>(block + DmgConvPct) =
			d2r::EvalCalc(game, attacker, skill, SkCalc4, hit->skillId, hit->level);
	}
	d2r::Engine<void (*)(Unit*, void*, int32_t, int32_t) noexcept>(ElementalDamage)(attacker, &damage, hit->skillId,
	                                                                               hit->level);
	AddSkillPhys(game, attacker, block, hit->skillId, hit->level);
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*, int32_t, uint8_t) noexcept>(WeaponDamage)(game, attacker, victim,
	                                                                                          &damage, 0, hit->srcDam);
	if (hit->damagePercent != 100) {  // PD2's 12-type table: the 9 types D2R's DamageEffects flags (TypeMirrored)
		for (const int offset : {DmgPhys, DmgFire, DmgLight, DmgMagic, DmgCold, DmgPoison, DamageLifeLeech,
		                         DamageManaLeech, DamageManaLeech + 4}) {  // + 4: stamina leech
			auto& field = *reinterpret_cast<int32_t*>(block + offset);
			field = static_cast<int32_t>(static_cast<int64_t>(field) * hit->damagePercent / 100);
		}
	}
	auto& leech = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&damage) + DamageLifeLeech);
	if (leech != 0) {
		leech = std::max(leech / hit->leechDivisor, 1u);
	}
	DealSplash(game, attacker, victim, damage);
	hit->victims += (damage.resultFlags & ResultHit) != 0 ? 1 : 0;
	ReleaseDamage(damage);
	return 1;
}

// inc_splash_radius on the layers of the weapon's item types (PD2's 0x10381720 with stat 478): the masteries' passive
// stats sit on their passiveitype's layer (One Hand Mastery 1han, Two Hand Mastery 2han)

}  // namespace pd2r::skills

namespace {

// inc_splash_radius on the layers of the weapon's item types (PD2's 0x10381720 with stat 478): the masteries' passive
// stats sit on their passiveitype's layer (One Hand Mastery 1han, Two Hand Mastery 2han)
auto WeaponTypeSplash(Unit* unit, Unit* weapon) noexcept -> int32_t {
	using namespace d2r::rva;
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(d2r::api.unitDataFlag(unit));
	if (weapon == nullptr || tables == nullptr) {
		return 0;
	}
	const auto isType = d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType);
	const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const int64_t types = At<int64_t>(tables, TablesItemTypesCount);
	int32_t sum = 0;
	for (int32_t type = 1; type < types; ++type) {  // layer 0 is the stat's own, which calc3 already counts
		if (isType(weapon, type) != 0) {
			sum += stat(unit, IncSplashRadius, type);
		}
	}
	return sum;
}

// A normal-tier base (its code is its normcode): PD2 takes 1 off the radius

// A normal-tier base (its code is its normcode): PD2 takes 1 off the radius
auto NormalBase(Game* game, Unit* weapon) noexcept -> bool {
	if (weapon == nullptr) {
		return false;
	}
	const auto* item = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::rva::ItemRecord)(
		d2r::DataFlag(game), At<int32_t>(weapon, d2r::rva::UnitClassId));
	return item != nullptr && At<uint32_t>(item, d2r::rva::ItCode) == At<uint32_t>(item, d2r::rva::ItNormCode);
}

// PD2 srvdo 163's block: a player's current skill (hard points, as 1.13c's skill +0x28) unless it is the splash
// itself, else Attack at level 1 with SrcDam 128

// PD2 srvdo 163's block: a player's current skill (hard points, as 1.13c's skill +0x28) unless it is the splash
// itself, else Attack at level 1 with SrcDam 128
auto SplashSkill(Game* game, Unit* unit, Unit* struck, int32_t splashId) noexcept -> SplashHit {
	using namespace d2r::rva;
	SplashHit hit {.struck = struck, .srcDam = 0x80, .skillId = 0, .level = 1, .percent = 0, .hitFlags = 0,
	               .resultFlags = 0, .hitClass = 0, .victims = 0};
	void* current = At<int32_t>(unit, UnitType) == 0 ? d2r::Engine<void* (*)(Unit*) noexcept>(CurrentSkill)(unit) : nullptr;
	const int32_t id = current != nullptr ? UnitSkillIdOf(current) : 0;
	if (current == nullptr || id == splashId) {
		return hit;
	}
	hit.skillId = id;
	hit.level = d2r::Engine<int32_t (*)(Unit*, void*, int32_t) noexcept>(UnitSkillLevel)(unit, current, 0);
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), id);
	if (skill == nullptr) {
		return hit;
	}
	hit.srcDam = At<uint8_t>(skill, SkSrcDam);
	hit.hitFlags = At<uint32_t>(skill, SkHitFlags);
	hit.resultFlags = At<uint16_t>(skill, SkResultFlags);
	hit.hitClass = At<int32_t>(skill, SkHitClass);
	const int field = Among(id, SplashCalc1) ? SkCalc1 : Among(id, SplashCalc2) ? SkCalc2 : 0;
	hit.percent = field != 0 ? d2r::EvalCalc(game, unit, skill, field, id, hit.level) : 0;
	return hit;
}

// The splash around `struck`. Returns 1 as PD2 does, also when nothing is in range or the struck unit has no
// position left (killed by the hit); 0 without a struck unit, or inside another splash.

}  // namespace

namespace pd2r::skills {

// The splash around `struck`. Returns 1 as PD2 does, also when nothing is in range or the struck unit has no
// position left (killed by the hit); 0 without a struck unit, or inside another splash.
auto Splash(Game* game, Unit* unit, Unit* struck, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr || struck == nullptr || t_splashing) {
		return 0;
	}
	Unit* weapon = d2r::Engine<Unit* (*)(Unit*) noexcept>(UnitWeapon)(unit);
	const int32_t calc3 = d2r::EvalCalc(game, unit, skill, SkCalc3, skillId, level);
	const int32_t typeSplash = WeaponTypeSplash(unit, weapon);
	const bool normal = NormalBase(game, weapon);
	const int32_t radius = calc3 + typeSplash / 20 - (normal ? 1 : 0);
	SplashHit hit = SplashSkill(game, unit, struck, skillId);
	int32_t x = 0, y = 0;
	if (radius > 0 && d2r::UnitXY(struck, &x, &y)) {
		d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(struck), x, y};
		t_splashing = true;
		d2r::Engine<AreaFn>(AreaUnits)(game, unit, &center, radius, AreaUnitMask, SplashOne, &hit, 0);
		t_splashing = false;
	}
	++g_splashes;
	return 1;
}

// PD2 srvdofunc 163. PD2 splashes around its path's target; D2R's event cast (item_splashonhit) names the struck unit as
// the skill's target, so that comes first.

}  // namespace pd2r::skills

namespace {

// PD2 srvdofunc 163. PD2 splashes around its path's target; D2R's event cast (item_splashonhit) names the struck unit as
// the skill's target, so that comes first.
auto SrvDoSplash(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	Unit* struck = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(d2r::rva::SkillTargetUnit)(game, unit);
	void* path = At<void*>(unit, d2r::rva::UnitPath);
	if (struck == nullptr && path != nullptr && At<int32_t>(unit, d2r::rva::UnitType) <= d2r::rva::UnitMonster) {
		struck = At<Unit*>(path, d2r::rva::PathTargetUnit);
	}
	if (struck == nullptr) {
		++g_splashes;  // a refused cast counts too
	}
	return Splash(game, unit, struck, skillId, level);
}

// --- Blade Sentinel's think (PD2 monai row 102, BladeCreeper: 0x1035fd10; #1869) ----------------------------------
// D2R's think (0x5c7900, found by its signature) keeps what PD2's keeps: it lives out calc4, makes the blade creeper
// missile once with the owner's attack rating and to-hit percent on the sentinel, and runs it between its two path
// points (stuck time 15, run 20). PD2's think adds, around D2R's:
// - on the missile's think, item_tohit_percent on the sentinel also gets the skill's ToHit (PD2's 0x1030b960, D2R's
//   SkillToHit): the blade creeper missile (ToHit 1) rolls with it; a monster owner (a Shadow Warrior's or Master's
//   sentinel) gives the sentinel tohit = its tohit + 5 x dexterity;
// - 6 frames after the missile, then at most every 12 frames, on a think where the sentinel reached a path point: an
//   area hit within aurarangecalc of the sentinel through PD2's callback 0x10303190 (SplashOne). The owner hits with
//   Blade Sentinel's damage and its weapon at SrcDam, after a to-hit roll with the skill's ToHit, full leech, 75% of
//   each damage type.
// PD2 keeps the next hit's frame in the think's missile flag (1.13c +0x1c); D2R's flag is the same field (+0x4c), which
// its think only tests for 0, so the frame goes there too. No PluginSDK service runs code on a monster's AI tick: the
// think registers on row 102 for bladecreeper only, through ai_registry (one table write).
// The owner: D2R's summon (0x56e6a0), as 1.13c's, walks a monster caster up to its player and makes the sentinel the
// player's pet. PD2's srvdo 44 (0x103aee30) then writes a monster caster into the sentinel's AI owner, so a shadow's
// sentinel hits as the shadow. Ported the same way: srvdo 44 is wrapped to remember a monster caster, and row 102's
// init (run inside the summon, after D2R set the owner) writes it into the AI owner that 0x4a53c0 reads. The pet list
// stays the player's, as in PD2.

}  // namespace

PD2R_ROUTE(29, ProcSplashDamage, SrvDoSplash)
PD2R_ROUTE(30, GolemSplash, SrvDoSplash)
PD2R_ROUTE(31, SkeletonSplash, SrvDoSplash)
