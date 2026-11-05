// PD2's item rules for Ormus' Robes (docs/research/p12-ormus-robes.md):
//
// - properties func 25 (`sorc-skill-rand-ctc`, ProjectDiablo.dll 0x103a12a0) in D2R's free property slot 26: one
//   random skill of PD2's sorceress list gets +param, and the item casts that skill on cast (item_skilloncast) at
//   chance min and level max, both adjusted by the skill as PD2 does.
// - skill on cast (item_skilloncast, PD2's itemeventfunc 33, 0x103473a0): after the server runs a skill a player
//   cast (mode SC; an inline hook on D2R's server skill dispatcher), each of the player's item_skilloncast listeners
//   rolls the stat's chance, then casts the layer's skill at the target, or at the caster's aim point. Every
//   cast-skill item works this way, not only Ormus' Robes. PD2 raises its dospellcast event for it; where was not
//   found, so this trigger is approximated (docs/deviations.md).
// - curses (docs/research/ir4-curses.md): max curses (a target holds up to the caster's max_curses + 1 curses) and
//   curse resistance (curse_effectiveness on the target scales the curse's stats), around D2R's ApplyCurse.
// - skill while equipped and phasing (docs/research/ir7-equipped-items.md): item_skillonequip runs its skill as an
//   item aura (D2R's item_aura case and tick, fed stat 375), and Phasing SelfAura drops the unit collision bits.
// - critical and deadly strike (IR3, docs/research/ir3-combat-stats.md): PD2's roll and multipliers in place of
//   D2R's, in WeaponDamage's roll block (a byte patch calling WeaponStrike) and D2R's missile roll.
// - op bases, life steal cap and crushing blow (IR3): equipped_eth / missing_hp on D2R's op-base read,
//   lifedrain_percentcap on D2R's add-life, and PD2's blow in CrushingBlowApply's jump slot.
#include "item_rules.h"

#include "d2r.h"
#include "log.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// PD2's list 8 (built at 0x10206ba0): the sorceress skills func 25 draws from, in PD2's order, as merged ids (Ice
// Barrage 369 -> 441, Lesser Hydra 383 -> 455, Combustion 376 -> 448; merge/ids-skills.tsv)
constexpr int32_t SorceressSkills[] = {39, 45, 44, 55, 59, 441, 64, 38, 49, 48, 53, 43, 36, 47, 46, 51, 56, 455, 62, 448};
constexpr int PropertySlot = 26;           // PD2's func 25; D2R's 25 is affix-rand (tools/pd2_ids.py FUNCS)
constexpr int32_t StatSingleSkill = 107;   // item_singleskill
constexpr int32_t StatSkillOnCast = 378;   // item_skilloncast, merged id (PD2 200, merge/ids-itemstatcost.tsv)
constexpr int32_t ItemTargetSelf = 5;      // ItemTarget of Nova and Frost Nova: PD2 casts them at the aim point

std::atomic<uint32_t> g_rolls {0};
std::atomic<uint32_t> g_raises {0};
std::atomic<uint32_t> g_procRolls {0};
std::atomic<uint32_t> g_procs {0};

using SetStatFn = int32_t (*)(uint8_t dataFlag, uint32_t a1, uint64_t a2, Unit* item, const void* property, uint8_t set,
                              uint16_t stat, uint16_t layer, uint32_t value, uint32_t a8, uint32_t a9,
                              uint64_t a10) noexcept;

auto DataTables(uint32_t dataFlag) noexcept -> const uint8_t* {
	return d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(d2r::rva::DataTables)(dataFlag);
}

// The skill / level split of a CTC layer, from the data D2R's own item events read it from
auto LayerShift(uint32_t dataFlag) noexcept -> uint32_t {
	return At<uint32_t>(DataTables(dataFlag), d2r::rva::TablesSkillLayerShift) & 0x1f;
}
auto LayerMask(uint32_t dataFlag) noexcept -> uint32_t {
	return At<uint32_t>(DataTables(dataFlag), d2r::rva::TablesSkillLayerMask);
}

// PD2's func 25 (0x103a12a0). Property: int16 code, int16 kind, int32 param (+4), min (+8), max (+0xc).
auto PropSorcSkillRandCtc(uint8_t dataFlag, uint32_t a1, uint64_t a2, Unit* item, const void* property, uint8_t set,
                          uint16_t stat, uint16_t val, int32_t first, uint32_t a8, uint32_t a9, uint64_t a10) noexcept
	-> int32_t {
	(void)stat;
	(void)val;
	(void)first;
	if (property == nullptr || item == nullptr) {
		return 0;
	}
	using namespace d2r::rva;
	constexpr int32_t count = static_cast<int32_t>(std::size(SorceressSkills));
	const int32_t pick = static_cast<int32_t>(
		d2r::Engine<uint64_t (*)(Unit*, int32_t, int32_t) noexcept>(ItemRandom)(item, 0, count - 1));
	const int32_t skill = SorceressSkills[pick >= 0 && pick < count ? pick : 0];
	const int32_t bonus = At<int32_t>(property, 4);
	int32_t chance = At<int32_t>(property, 8);
	int32_t level = At<int32_t>(property, 0xc);
	if (const uint8_t* record = d2r::api.getSkillRecord(dataFlag, skill)) {
		if (At<int32_t>(record, SkLocalDelay) > 0) {  // a skill with a cooldown procs at 67% of the chance
			chance = static_cast<int32_t>(static_cast<double>(chance) * 67.0 / 100.0);
		}
		level -= (At<uint16_t>(record, SkReqLevel) - 6) / 6;  // a later skill procs at a lower level
		level = level < 1 ? 1 : level;
	}
	const auto setStat = d2r::Engine<SetStatFn>(PropertySetStat);
	const int32_t result = setStat(dataFlag, a1, a2, item, property, set, StatSingleSkill, static_cast<uint16_t>(skill),
	                               static_cast<uint32_t>(bonus), a8, a9, a10);
	const uint32_t layer = (static_cast<uint32_t>(skill) << LayerShift(dataFlag)) |
	                       (static_cast<uint32_t>(level) & LayerMask(dataFlag));
	setStat(dataFlag, a1, a2, item, property, set, StatSkillOnCast, static_cast<uint16_t>(layer),
	        static_cast<uint32_t>(chance), a8, a9, a10);
	const uint32_t n = ++g_rolls;
	plog::Info("ormus: rolled skill %d +%d, cast on cast %d%% level %d (%u so far)", skill, bonus, chance, level, n);
	return result;
}

// PD2's itemeventfunc 33 (0x103473a0), called with the listener's stat << 16 | layer
auto ItemEventSkillOnCast(Game* game, int32_t event, Unit* unit, Unit* target, void* damage, uint32_t statLayer,
                          int32_t unused) noexcept -> int32_t {
	(void)event;
	(void)damage;
	(void)unused;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	using namespace d2r::rva;
	const uint32_t layer = statLayer & 0xffff;
	const int32_t chance = d2r::Engine<int32_t (*)(Unit*, uint32_t, uint32_t) noexcept>(UnitLayerStat)(
		unit, statLayer >> 16, layer);
	if (chance < 1) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const int32_t skill = static_cast<int32_t>(layer >> LayerShift(dataFlag));
	const int32_t level = static_cast<int32_t>(layer & LayerMask(dataFlag));
	auto* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	const int32_t roll = static_cast<int32_t>(d2r::RandomBelow(seed, 100));
	++g_procRolls;
	const uint8_t* record = roll < chance ? d2r::api.getSkillRecord(dataFlag, skill) : nullptr;
	if (record == nullptr) {
		return 0;
	}
	const auto setFlag = d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(SetUnitFlag);
	setFlag(unit, UnitFlagEventCast, 1);  // no dospellcast from the proc itself (SkillDispatch)
	int64_t cast = 0;
	const bool atTarget = target != nullptr && At<uint8_t>(record, SkItemTarget) != ItemTargetSelf;
	if (atTarget) {
		cast = d2r::Engine<int64_t (*)(Unit*, int32_t, int32_t, Unit*, int32_t) noexcept>(CastEventSkill)(
			unit, skill, level, target, 1);
	} else {
		int32_t x = 0, y = 0;
		if (d2r::api.skillTargetXY(game, unit, &x, &y) != 0 || d2r::UnitXY(unit, &x, &y)) {
			cast = d2r::Engine<int64_t (*)(Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(
				CastEventSkillAt)(unit, skill, level, x, y, 0);
		}
	}
	setFlag(unit, UnitFlagEventCast, 0);
	const uint32_t n = ++g_procs;
	if (n <= 20 || n % 100 == 0) {
		plog::Info("skill on cast: %d%% (roll %d) skill %d level %d at %s: %lld, %u so far", chance, roll, skill, level,
		           atTarget ? "target" : "point", static_cast<long long>(cast), n);
	}
	return 1;
}

// item_skilloncast's itemevent1 (dospellcast, 18 in the merged events.txt), read from the stat's record so a
// renumbered events.txt keeps working; -1 when the row has none
auto SpellCastEvent(uint32_t dataFlag) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t* tables = DataTables(dataFlag);
	if (tables == nullptr || static_cast<uint64_t>(StatSkillOnCast) >= At<uint64_t>(tables, TablesItemStatCostCount)) {
		return -1;
	}
	const uint8_t* records = At<const uint8_t*>(tables, TablesItemStatCost);
	return records != nullptr ? At<int16_t>(records + StatSkillOnCast * IscRecordSize, IscItemEvent1) : -1;
}

// The caster's registered event listeners (unit +0xe0, D2R's own list: equipping an item adds its stat listeners
// there). PD2 raises dospellcast and D2R would call each listener's
// itemeventfunc, but D2RCore routes item listeners through its own copy of the function table, which a plugin cannot
// reach; so this calls PD2's event for each item_skilloncast listener itself.
auto CastOnCastListeners(Game* game, Unit* unit, Unit* target, int32_t event) noexcept -> void {
	using namespace d2r::rva;
	int guard = 0;  // a unit holds a few dozen listeners; 256 only stops a corrupt list from spinning
	for (const uint8_t* node = At<const uint8_t*>(unit, UnitEventList); node != nullptr && guard < 256;
	     node = At<const uint8_t*>(node, EventNodeNext), ++guard) {
		if (At<int32_t>(node, EventNodeEvent) == event && At<int32_t>(node, EventNodeKind) == EventNodeKindItem &&
		    At<uint32_t>(node, EventNodeStat) == static_cast<uint32_t>(StatSkillOnCast)) {
			ItemEventSkillOnCast(game, event, unit, target, nullptr,
			                     (static_cast<uint32_t>(StatSkillOnCast) << 16) | (At<uint32_t>(node, EventNodeLayer) & 0xffff), 0);
		}
	}
}

using DispatchFn = uint64_t (*)(Game* game, Unit* unit, int32_t skillId, int32_t level, int32_t a5, int32_t a6,
                                int32_t a7) noexcept;
DispatchFn g_dispatchOriginal = nullptr;

// D2R's server skill dispatcher, then dospellcast on a player who just cast (mode SC) a skill by hand.
// shortcut: PD2's own raise site was not found; upgrade if it turns up (docs/research/p12-ormus-robes.md)
auto SkillDispatch(Game* game, Unit* unit, int32_t skillId, int32_t level, int32_t a5, int32_t a6, int32_t a7) noexcept
	-> uint64_t {
	const uint64_t ran = g_dispatchOriginal(game, unit, skillId, level, a5, a6, a7);
	using namespace d2r::rva;
	if (ran == 0 || game == nullptr || unit == nullptr || At<int32_t>(unit, UnitType) != 0 ||
	    At<int32_t>(unit, UnitMode) != PlayerModeCast || (At<uint32_t>(unit, UnitFlags) & UnitFlagEventCast) != 0) {
		return ran;
	}
	const int32_t event = SpellCastEvent(d2r::DataFlag(game));
	if (event >= 0) {
		CastOnCastListeners(game, unit, d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit), event);
		if (plog::Sample(g_raises)) {
			plog::Info("dospellcast: event %d after skill %d level %d, %u so far", event, skillId, level, g_raises.load());
		}
	}
	return ran;
}

// --- Critical and deadly strike (IR3, docs/research/ir3-combat-stats.md) ---
// Merged stat ids (merge/ids-itemstatcost.tsv; PD2's 337, 141, 256, 257, 258, 210)
constexpr uint32_t StatPassiveCrit = 337;     // passive_critical_strike
constexpr uint32_t StatDeadlyStrike = 141;    // item_deadlystrike
constexpr uint32_t StatCritMultiplier = 390;  // item_crit_multiplier
constexpr uint32_t StatDsMultiplier = 391;    // item_ds_multiplier
constexpr uint32_t StatCritChance = 392;      // item_crit_chance
constexpr uint32_t StatMaxDeadly = 386;       // item_maxdeadlystrike
constexpr int32_t StrikeCap = 75;

std::atomic<uint32_t> g_strikeRolls {0};
std::atomic<uint32_t> g_crits {0};
std::atomic<uint32_t> g_deadlyStrikes {0};

auto Stat(Unit* unit, uint32_t stat) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*, uint32_t, uint32_t) noexcept>(d2r::rva::UnitStat)(unit, stat, 0);
}

struct Strike {
	int kind = 0;  // 0 none, 1 critical strike, 2 deadly strike
	int32_t critChance = 0, critRoll = -1, dsChance = 0, dsCap = 0, dsRoll = -1;
};

// PD2's roll (ProjectDiablo.dll 0x10304710): crit = the weapon mastery's crit + passive_critical_strike +
// item_crit_chance, at most 75; only when it fails, deadly strike at most 75 + item_maxdeadlystrike
auto RollStrike(Unit* unit, Unit* weapon) noexcept -> Strike {
	using namespace d2r::rva;
	Strike s;
	const int32_t mastery = weapon != nullptr
		? d2r::Engine<int32_t (*)(Unit*, Unit*, int32_t, int32_t) noexcept>(MasteryBonus)(unit, weapon, 0, 2) : 0;
	s.critChance = mastery + Stat(unit, StatPassiveCrit) + Stat(unit, StatCritChance);
	auto* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	if (s.critChance > 0) {
		s.critChance = s.critChance < StrikeCap ? s.critChance : StrikeCap;
		s.critRoll = static_cast<int32_t>(d2r::RandomBelow(seed, 100));
		if (s.critRoll < s.critChance) {
			s.kind = 1;
			return s;
		}
	}
	s.dsCap = StrikeCap + Stat(unit, StatMaxDeadly);
	s.dsChance = Stat(unit, StatDeadlyStrike);
	if (s.dsChance > 0) {
		s.dsChance = s.dsChance < s.dsCap ? s.dsChance : s.dsCap;
		s.dsRoll = static_cast<int32_t>(d2r::RandomBelow(seed, 100));
		s.kind = s.dsRoll < s.dsChance ? 2 : 0;
	}
	return s;
}

// Every roll of a player (the game check's attacker) is logged; other units' a sample
auto LogStrike(const char* where, Unit* unit, const Strike& s, uint32_t before, uint32_t after, int32_t multiplier) noexcept
	-> void {
	const uint32_t n = ++g_strikeRolls;
	if (s.kind == 1) {
		++g_crits;
	} else if (s.kind == 2) {
		++g_deadlyStrikes;
	}
	const int32_t type = At<int32_t>(unit, d2r::rva::UnitType);
	if (type == 0 || n <= 20 || n % 500 == 0) {
		plog::Info("strike: %s unit type %d kind %d, crit %d roll %d, deadly %d (cap %d) roll %d, physical %u -> %u (x%d%%), %u so far",
		           where, type, s.kind, s.critChance, s.critRoll, s.dsChance, s.dsCap, s.dsRoll, before, after, multiplier, n);
	}
}

// Called from WeaponDamage's patched roll block (WeaponCritBlock) in place of D2R's: PD2's roll, then the physical
// part (damage +0x18) times 200 + item_crit_multiplier or 150 + item_ds_multiplier percent (0x103047f0), flag 0x2000.
// a5 keeps D2R's gate on the mastery's crit (D2R adds it only when a5 is 0).
// shortcut: PD2 adds the multiplier's per-hand item sum (0x10381720); the unit's total is used, which differs only
// when dual wielding two multiplier weapons; upgrade if a dual-wield carrier turns up.
auto WeaponStrike(Unit* attacker, uint8_t* damage, int32_t a5) noexcept -> void {
	using namespace d2r::rva;
	if (attacker == nullptr || damage == nullptr) {
		return;
	}
	Unit* weapon = a5 == 0 ? d2r::Engine<Unit* (*)(Unit*) noexcept>(UnitWeapon)(attacker) : nullptr;
	const Strike s = RollStrike(attacker, weapon);
	const uint32_t before = At<uint32_t>(damage, 0x18);
	int32_t multiplier = 100;
	if (s.kind != 0) {
		multiplier = s.kind == 1 ? 200 + Stat(attacker, StatCritMultiplier) : 150 + Stat(attacker, StatDsMultiplier);
		const int64_t raised = static_cast<int64_t>(before) * (multiplier > 0 ? multiplier : 0) / 100;
		*reinterpret_cast<uint32_t*>(damage + 0x18) = static_cast<uint32_t>(raised < 0x7fffffff ? raised : 0x7fffffff);
		*reinterpret_cast<uint16_t*>(damage + 4) |= 0x2000;
	}
	LogStrike("weapon", attacker, s, before, At<uint32_t>(damage, 0x18), multiplier);
}

// D2R's missile roll (MissileCritRoll) with PD2's chances; the hit still doubles (D2R's; approximated, docs/research/ir3-combat-stats.md)
auto MissileStrike(Unit* unit, Unit* weapon) noexcept -> uint64_t {
	if (unit == nullptr) {
		return 0;
	}
	const Strike s = RollStrike(unit, weapon);
	LogStrike("missile", unit, s, 0, 0, s.kind == 0 ? 100 : 200);
	return s.kind != 0 ? 1 : 0;
}

using MissileRollFn = uint64_t (*)(Unit*, Unit*) noexcept;
MissileRollFn g_missileRollOriginal = nullptr;  // not called: PD2's roll replaces D2R's

// WeaponCritBlock's new bytes: mov rcx,rsi / mov rdx,rdi / mov r8d,r14d / mov rax,WeaponStrike / call rax /
// jmp WeaponCritBlockEnd / nop nop. The block's own volatile registers are set again after 0x44c3d9.
auto WeaponCritPatch(uint8_t (&out)[28]) noexcept -> void {
	using namespace d2r::rva;
	static_assert(sizeof(WeaponCritBlockBytes) == 28);
	const uint8_t head[] = {0x48, 0x8b, 0xce, 0x48, 0x8b, 0xd7, 0x45, 0x8b, 0xc6, 0x48, 0xb8};
	std::memcpy(out, head, sizeof(head));
	const uint64_t target = reinterpret_cast<uint64_t>(&WeaponStrike);
	std::memcpy(out + 11, &target, 8);
	out[19] = 0xff;
	out[20] = 0xd0;
	out[21] = 0xe9;
	const int32_t rel = static_cast<int32_t>(WeaponCritBlockEnd - (WeaponCritBlock + 26));
	std::memcpy(out + 22, &rel, 4);
	out[26] = 0x90;
	out[27] = 0x90;
}

// --- Op bases (IR3): PD2's 0x102fa640 computes two stats no one sets, for ops 4 / 5 (item_dmgpercent_pereth,
// item_dmgpercent_permissinghppercent). Merged ids (PD2's 183, 184).
constexpr uint32_t StatEquippedEth = 368;  // equipped_eth
constexpr uint32_t StatMissingHp = 369;    // missing_hp
constexpr uint32_t StatLife = 6;
constexpr uint32_t StatMaxLife = 7;
constexpr int32_t BodyLocFirst = 1, BodyLocLast = 10;  // worn; 11 and 12 hold the swapped weapons

std::atomic<uint32_t> g_ethReads {0};

using BaseStatFn = int32_t (*)(const uint8_t* statList, uint32_t statLayer, const uint8_t* record) noexcept;
BaseStatFn g_baseStatOriginal = nullptr;

// The ethereal items a player or monster wears
auto WornEthereal(Unit* unit) noexcept -> int32_t {
	using namespace d2r::rva;
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory)(unit, __FILE__, __LINE__);
	if (inventory == nullptr) {
		return 0;
	}
	const auto bodyItem = d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(InventoryBodyItem);
	const auto ethereal = d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemIsEthereal);
	int32_t count = 0;
	for (int32_t loc = BodyLocFirst; loc <= BodyLocLast; ++loc) {
		Unit* item = bodyItem(inventory, loc);
		count += item != nullptr && ethereal(item) != 0 ? 1 : 0;
	}
	return count;
}

// PD2's 0x102fa640: equipped_eth and missing_hp of the statlist's player or monster, else D2R's read.
// PD2's third case, inc_splash_radius + missing_hp x inc_splash_radius_permissinghp (at most 40), is only reached
// with inc_splash_radius as an op base, which no stat has: not ported (docs/research/ir3-combat-stats.md).
auto BaseStat(const uint8_t* statList, uint32_t statLayer, const uint8_t* record) noexcept -> int32_t {
	const uint32_t stat = statLayer >> 16;
	if (statList == nullptr || (stat != StatEquippedEth && stat != StatMissingHp)) {
		return g_baseStatOriginal(statList, statLayer, record);
	}
	auto* unit = At<Unit*>(statList, d2r::rva::StatListUnit);
	const int32_t type = unit != nullptr ? At<int32_t>(unit, d2r::rva::UnitType) : -1;
	if (type != 0 && type != 1) {
		return 0;
	}
	if (stat == StatMissingHp) {
		const int64_t life = Stat(unit, StatLife), maxLife = Stat(unit, StatMaxLife);
		return maxLife > 0 ? static_cast<int32_t>((maxLife - life) * 100 / maxLife) : 0;
	}
	const int32_t count = WornEthereal(unit);
	if (plog::Sample(g_ethReads)) {
		plog::Info("equipped_eth: unit type %d wears %d ethereal, %u reads so far", type, count, g_ethReads.load());
	}
	return count;
}

// --- Life steal cap (IR3, Nightmare's Feast): lifedrain_percentcap, merged id 541 (PD2's 488) ---
constexpr uint32_t StatLifeDrainCap = 541;

std::atomic<uint32_t> g_capChecks {0};
std::atomic<uint32_t> g_capBlocks {0};

using AddLifeFn = int32_t (*)(Unit*, int32_t) noexcept;
AddLifeFn g_addLifeOriginal = nullptr;

// PD2's 0x10306ca0 sets life through 0x102fbc50: with lifedrain_percentcap C, the new life is set only when it, or
// the life before, is under (100 - C)% of the max (stat 7); else nothing is added.
auto CappedAddLife(Unit* unit, int32_t amount) noexcept -> int32_t {
	const int32_t cap = unit != nullptr && amount > 0 ? Stat(unit, StatLifeDrainCap) : 0;
	if (cap == 0) {
		return g_addLifeOriginal(unit, amount);
	}
	const int64_t life = Stat(unit, StatLife);
	const int64_t maxLife = std::max(Stat(unit, StatMaxLife), 1);
	const int64_t top = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::UnitMaxLife)(unit);
	const int64_t after = life + amount < top ? life + amount : top;
	const double limit = (100.0 - cap) / 100.0;
	const bool allowed = static_cast<double>(after) / maxLife < limit || static_cast<double>(life) / maxLife < limit;
	const uint32_t n = ++g_capChecks;
	g_capBlocks += allowed ? 0 : 1;
	if (n <= 20 || n % 200 == 0) {
		plog::Info("life steal cap: unit type %d cap %d%%, life %lld -> %lld of %lld (1/256): %s, %u blocked of %u",
		           At<int32_t>(unit, d2r::rva::UnitType), cap, static_cast<long long>(life), static_cast<long long>(after),
		           static_cast<long long>(maxLife), allowed ? "added" : "blocked", g_capBlocks.load(), n);
	}
	return allowed ? g_addLifeOriginal(unit, amount) : 0;
}

// --- Crushing blow (IR3): PD2's blow (event 16, 0x103469a0) in place of D2R's CrushingBlowApply ---
constexpr uint32_t StatDamageResist = 36;
constexpr uint32_t StatPlayerCount = 100;   // monster_playercount
constexpr uint32_t StatCbEfficiency = 394;  // item_crushingblow_efficiency (PD2's 268)
constexpr int32_t OverlayCrushingBlow = 147;  // PD2's 0x93
constexpr int32_t Hirelings[] = {271, 338, 359, 560, 561};  // 1.13c's D2Common #11104

std::atomic<uint32_t> g_blows {0};

// PD2's divisor: 10 on a player or hireling; on a prime evil (monstats flag) 70 + 10 per missing life percent (1.13c's
// D2Common #11027 is the life percent); 8 on others; a monster then adds its player count's life bonus / 50.
// shortcut: PD2's divisor 30 for its own boss lists (0x10371290, built at run time) is not ported; those bosses
// are PD2 content not in the merged data. Upgrade with them.
auto BlowDivisor(Game* game, Unit* target) noexcept -> double {
	using namespace d2r::rva;
	const int32_t type = At<int32_t>(target, UnitType);
	if (type == 0) {
		return 10.0;
	}
	if (type != 1) {
		return 8.0;
	}
	const int32_t classId = At<int32_t>(target, UnitClassId);
	for (int32_t hireling : Hirelings) {
		if (classId == hireling) {
			return 10.0;
		}
	}
	double divisor = 8.0;
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(d2r::DataFlag(game), classId);
	if (record != nullptr && (At<uint32_t>(record, MonFlags) & MonPrimeEvil) != 0) {
		const int32_t maxLife = Stat(target, StatMaxLife) >> 8;
		const int32_t lifePercent = maxLife != 0 ? (Stat(target, StatLife) >> 8) * 100 / maxLife : 0;
		divisor = (100.0 - lifePercent) * 10.0 + 70.0;
	}
	const int32_t players = std::max(Stat(target, StatPlayerCount), 1);
	const int32_t bonus = players < 9 ? (players > 1 ? (players - 1) * 50 : 0) : (players - 2) * 50;  // D2Game 0xaf5f0
	return divisor + bonus / 50.0;
}

// PD2's blow: life * (1 + item_crushingblow_efficiency / 100) / divisor (x1.5 for a missile), less the target's
// damageresist percent, nothing at 100 or more; then the kill flag and the overlay. D2R's event 16 and Smite's
// roll (skill_functions.cpp) both call it.
// shortcut: PD2 adds Smite's calc4 to the effectiveness and the efficiency's per-hand item sum; neither is passed in
// here (1 and the unit's total). Upgrade with a Smite carrier of item_crushingblow_efficiency.
auto CrushingBlow(Game* game, Unit* unit, Unit* target, d2r::CrushingBlowArgs* args) noexcept -> int32_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr || target == nullptr || args == nullptr) {
		return 0;
	}
	const int32_t resist = Stat(target, StatDamageResist);
	if (resist >= 100) {
		return 0;
	}
	const double divisor = BlowDivisor(game, target) * (args->missile != 0 ? 1.5 : 1.0);
	const int32_t efficiency = Stat(unit, StatCbEfficiency);
	const double life = static_cast<uint32_t>(Stat(target, StatLife));
	double blow = life / (divisor / (efficiency / 100.0 + 1.0));
	blow -= static_cast<int32_t>(static_cast<int64_t>(static_cast<int32_t>(blow)) * resist / 100);  // 0x1037c4c0
	const int32_t after = std::max(static_cast<int32_t>(life - blow), 0);
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t, uint32_t) noexcept>(SetUnitStat)(target, StatLife, after, 0);
	if (after < 1 && args->damage != nullptr) {
		*reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(args->damage) + DamageResult) |= 2;  // killed
	}
	if (blow > 0) {
		d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetOverlay)(target, OverlayCrushingBlow, 0);
	}
	const uint32_t n = ++g_blows;
	if (n <= 20 || n % 200 == 0) {
		plog::Info("crushing blow: unit type %d on unit type %d class %d%s: divisor %.2f, efficiency %d, resist %d, "
		           "life %.0f -> %d (1/256), %u so far", At<int32_t>(unit, UnitType), At<int32_t>(target, UnitType),
		           At<int32_t>(target, UnitClassId), args->missile != 0 ? " (missile)" : "", divisor, efficiency, resist,
		           life, after, n);
	}
	return 1;
}

// --- Curses: curse resistance and max curses (docs/research/ir4-curses.md) ---------------------------------------
constexpr int32_t StatCurseSlots = 374;          // curse_slots, merged id (PD2 190): the slot count a curse was cast with
constexpr int32_t StatMaxCurses = 421;           // max_curses (PD2 368)
constexpr int32_t StatCurseResistance = 557;     // curse_effectiveness, shown as Curse Resistance (PD2 504)
constexpr int32_t CurseResistanceCap = 75;
constexpr int MaxCurseLists = 64;  // a unit holds a handful; more only stops a corrupt chain from spinning

std::atomic<uint32_t> g_curses {0};
std::atomic<uint32_t> g_curseResisted {0};

// PD2's 0x1037c4c0 as its curse code calls it: value * (100 - resistance) / 100, truncated
auto Resisted(int32_t value, int32_t resistance) noexcept -> int32_t {
	const double v = static_cast<double>(value) * (100.0 - resistance) / 100.0;
	return v > 2147483647.0 ? 2147483647 : static_cast<int32_t>(v);
}

auto StatOf(Unit* unit, int32_t stat, int32_t layer = 0) noexcept -> int32_t {
	return unit != nullptr ? d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(unit, stat, layer)
	                       : 0;
}

// PD2's slot rule (ProjectDiablo.dll 0x1036d7d0) before D2R's ApplyCurse runs. D2R's NewStateList removes the first
// curse list it finds, so every curse list that must stay loses its curse flag until ApplyCurse returns (`hidden`).
// Returns the caster's slot count, which the new curse keeps (stat curse_slots).
auto MakeRoom(uint32_t dataFlag, Unit* caster, Unit* target, int32_t state, uint8_t* (&hidden)[MaxCurseLists],
              int& hiddenCount) noexcept -> int32_t {
	using namespace d2r::rva;
	const auto listStat = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue);
	const auto setStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	const int32_t own = StatOf(caster, StatMaxCurses) + 1;
	const uint8_t* owner = At<const uint8_t*>(target, UnitListOwner);
	if (owner == nullptr || At<int32_t>(owner, ListOwnerMark) >= 0) {
		return own;
	}
	// The most slots any curse on the target was cast with; a curse cast with fewer drops its count (PD2 does so)
	int32_t slots = own;
	int count = 0;
	uint8_t* oldest = nullptr;  // the last timed curse in the chain, the one a full target loses
	int guard = 0;
	for (uint8_t* list = At<uint8_t*>(owner, ListOwnerFirst); list != nullptr && guard < MaxCurseLists;
	     list = At<uint8_t*>(list, ListNext), ++guard) {
		if ((At<uint32_t>(list, ListFlags) & ListFlagCurse) == 0) {
			continue;
		}
		++count;
		const int32_t kept = listStat(dataFlag, list, StatCurseSlots, 0);
		if (kept != 0 && kept < slots) {
			setStat(dataFlag, list, StatCurseSlots, 0, 0);
		} else if (kept > slots) {
			slots = kept;
		}
		if ((At<uint32_t>(list, ListFlags) & ListFlagTimed) != 0) {
			oldest = list;
		}
	}
	// The one curse list NewStateList may see: this state's own (it refreshes or replaces it), else none while a slot
	// is free, else the oldest timed curse
	uint8_t* same = d2r::Engine<uint8_t* (*)(Unit*, int32_t) noexcept>(UnitStateList)(target, state);
	uint8_t* seen = same != nullptr ? same : count < slots ? nullptr : oldest;
	guard = 0;
	for (uint8_t* list = At<uint8_t*>(owner, ListOwnerFirst); list != nullptr && guard < MaxCurseLists;
	     list = At<uint8_t*>(list, ListNext), ++guard) {
		if ((At<uint32_t>(list, ListFlags) & ListFlagCurse) != 0 && list != seen) {
			*reinterpret_cast<uint32_t*>(list + ListFlags) &= ~ListFlagCurse;
			hidden[hiddenCount++] = list;
		}
	}
	const uint32_t n = ++g_curses;
	if (n <= 20 || n % 100 == 0) {
		plog::Info("curse: state %d, %d curse(s) on the target, %d slot(s) (caster %d), %s, %u so far", state, count,
		           slots, own, same != nullptr ? "same state" : seen != nullptr ? "replaces the oldest" : "added", n);
	}
	return own;
}

// PD2's curse resistance (stat curse_effectiveness on the cursed unit), on the list ApplyCurse just set: aurastat1,
// which D2R sets only on a new list, at most 75 on a player (0x1036d7d0); aurastat2-6 at most 75 (0x102fad50, PD2's
// stat setter for curse lists)
auto ResistCurse(uint32_t dataFlag, Unit* target, const d2r::CurseParams* params, void* list, bool fresh) noexcept
	-> void {
	using namespace d2r::rva;
	const int32_t resistance = StatOf(target, StatCurseResistance);
	if (resistance == 0) {
		return;
	}
	const auto value = d2r::Engine<int32_t (*)(Game*, Unit*, int32_t, int32_t) noexcept>(CurseStatValue);
	const auto setStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	const bool player = At<int32_t>(target, UnitType) == 0;
	for (int i = fresh ? 0 : 1; i < 6; ++i) {
		const int32_t stat = params->stats[i];
		if (stat < 0) {
			continue;
		}
		const int32_t base = value(params->game, target, stat, params->values[i]);
		if (base == 0) {
			continue;  // D2R set nothing for it
		}
		// shortcut: an aurastat1 resisted to 0 leaves the curse on with the stat at 0 (PD2 cancels it); upgrade by
		// resisting params->values[0] before the call if a unit ever shows it
		const int32_t used = resistance > CurseResistanceCap && (i > 0 || player) ? CurseResistanceCap : resistance;
		const int32_t resisted = Resisted(base, used);
		setStat(dataFlag, list, stat, resisted, 0);
		const uint32_t n = ++g_curseResisted;
		if (n <= 20 || n % 100 == 0) {
			plog::Info("curse resistance %d (used %d) on %s: aurastat%d stat %d %d -> %d, %u so far", resistance, used,
			           player ? "player" : "monster", i + 1, stat, base, resisted, n);
		}
	}
}

using ApplyCurseFn = int64_t (*)(Unit* target, d2r::CurseParams* params) noexcept;
ApplyCurseFn g_applyCurseOriginal = nullptr;

auto CurseApply(Unit* target, d2r::CurseParams* params) noexcept -> int64_t {
	using namespace d2r::rva;
	if (target == nullptr || params == nullptr || params->game == nullptr) {
		return g_applyCurseOriginal(target, params);
	}
	const uint32_t dataFlag = d2r::DataFlag(params->game);
	const int32_t state = params->state;
	if (state < 0 || d2r::Engine<uint32_t (*)(uint32_t, int32_t) noexcept>(StateIsCurse)(dataFlag, state) == 0) {
		return g_applyCurseOriginal(target, params);
	}
	const auto stateList = d2r::Engine<uint8_t* (*)(Unit*, int32_t) noexcept>(UnitStateList);
	uint8_t* before = stateList(target, state);
	uint8_t* hidden[MaxCurseLists];
	int hiddenCount = 0;
	const int32_t slots = MakeRoom(dataFlag, params->caster, target, state, hidden, hiddenCount);
	const int64_t applied = g_applyCurseOriginal(target, params);
	for (int i = 0; i < hiddenCount; ++i) {
		*reinterpret_cast<uint32_t*>(hidden[i] + ListFlags) |= ListFlagCurse;
	}
	uint8_t* list = applied != 0 ? stateList(target, state) : nullptr;
	if (list == nullptr) {
		return applied;
	}
	const bool fresh = list != before;
	if (fresh && slots > StatOf(target, StatCurseSlots)) {
		d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat)(dataFlag, list,
		                                                                                      StatCurseSlots, slots, 0);
	}
	ResistCurse(dataFlag, target, params, list, fresh);
	const uint32_t n = g_curses.load();
	if (n <= 20 || n % 100 == 0) {
		int held = 0;  // read back from the target, so the game check sees what D2R kept
		const uint8_t* owner = At<const uint8_t*>(target, UnitListOwner);
		int guard = 0;
		for (const uint8_t* l = owner != nullptr ? At<const uint8_t*>(owner, ListOwnerFirst) : nullptr;
		     l != nullptr && guard < MaxCurseLists; l = At<const uint8_t*>(l, ListNext), ++guard) {
			held += (At<uint32_t>(l, ListFlags) & ListFlagCurse) != 0 ? 1 : 0;
		}
		plog::Info("curse: state %d landed, the target holds %d curse(s)", state, held);
	}
	return applied;
}

// --- Skill while equipped and phasing (docs/research/ir7-equipped-items.md) -------------------------------------
// PD2 copies D2's item aura for item_skillonequip (0x1036f640, 0x10345cc0): the skill runs as an item aura at the
// stat's level. D2R's stat callback has no case for stat 375, so core-rules hands it item_aura's case, and the aura's
// tick reads item_skillonequip where it reads item_aura.
constexpr int32_t StatItemAura = 151;
constexpr int32_t StatSkillOnEquip = 375;  // item_skillonequip, merged id (PD2 191)
constexpr int32_t SkillPhasing = 674;      // Phasing SelfAura, merged id (PD2 605): Spectral Plate
constexpr int32_t SkillCharge = 107, SkillMonCharge = 486, SkillArmageddonSelfAura = 662;  // merged (PD2 107, 415, 593)
// Path collision bits PD2's phasing (0x10380c50) clears and sets back: 0x1c09 <-> 0xc09 on a player, 0x3c01 <-> 0xc01
// on a monster (a mercenary)
constexpr uint32_t PhasingBitsPlayer = 0x1000, PhasingBitsMonster = 0x3000;
constexpr int32_t UnitPlayer = 0;

std::atomic<uint32_t> g_equipCallbacks {0};
std::atomic<uint32_t> g_equipTicks {0};
std::atomic<uint32_t> g_phasing {0};

// PD2's 0x10380c50: while Phasing SelfAura is equipped, the unit's path leaves out the unit collision bits
auto SetPhasing(Unit* unit, bool on) noexcept -> void {
	using namespace d2r::rva;
	uint8_t* path = At<uint8_t*>(unit, UnitPath);
	const int32_t type = At<int32_t>(unit, UnitType);
	if (path == nullptr || (type != UnitPlayer && type != UnitMonster)) {
		return;
	}
	const uint32_t bits = type == UnitPlayer ? PhasingBitsPlayer : PhasingBitsMonster;
	const uint32_t mask = At<uint32_t>(path, PathCollisionMask);
	const uint32_t want = on ? mask & ~bits : mask | bits;
	if (want == mask) {
		return;
	}
	d2r::Engine<void (*)(uint8_t*, uint32_t) noexcept>(PathSetCollisionMask)(path, want);
	plog::Info("phasing: %s, unit type %d, collision mask 0x%x -> 0x%x, %u so far", on ? "on" : "off", type, mask,
	           want, ++g_phasing);
}

using StatCallbackFn = uint64_t (*)(Game* game, Unit* unit, Unit* item, uint64_t statLayer, int32_t oldValue,
                                    int32_t newValue) noexcept;

// Every unit's stat callback (see StatCallbackInit): item_skillonequip goes to D2R's as item_aura, same layer. The
// stat id is r9's high dword, the layer its low one (seen in game: 375/624 arrives as 0x17700000270)
auto EquipStatCallback(Game* game, Unit* unit, Unit* item, uint64_t statLayer, int32_t oldValue, int32_t newValue) noexcept
	-> uint64_t {
	const auto original = d2r::Engine<StatCallbackFn>(d2r::rva::StatCallback);
	if ((statLayer >> 32) != static_cast<uint64_t>(StatSkillOnEquip)) {
		return original(game, unit, item, statLayer, oldValue, newValue);
	}
	const uint32_t layer = static_cast<uint32_t>(statLayer);
	const uint64_t result = original(game, unit, item, (static_cast<uint64_t>(StatItemAura) << 32) | layer, oldValue,
	                                 newValue);
	if (unit != nullptr) {
		if (layer == static_cast<uint32_t>(SkillPhasing)) {
			SetPhasing(unit, newValue > 0);  // item_aura's case starts the aura at newValue (0x4705f6)
		}
		const uint32_t n = ++g_equipCallbacks;
		if (n <= 20 || n % 100 == 0) {
			plog::Info("skill on equip: unit type %d, skill %u, %d -> %d, level now %d, %u so far",
			           At<int32_t>(unit, d2r::rva::UnitType), layer, oldValue, newValue,
			           StatOf(unit, StatSkillOnEquip, static_cast<int32_t>(layer)), n);
		}
	}
	return result;
}

using StatCallbackInitFn = uint64_t (*)(Unit* unit, uint32_t flags, void* callback, void* arg) noexcept;
StatCallbackInitFn g_statCallbackInitOriginal = nullptr;

auto EquipStatCallbackInit(Unit* unit, uint32_t flags, void* callback, void* arg) noexcept -> uint64_t {
	if (reinterpret_cast<uint64_t>(callback) == d2r::imageBase + d2r::rva::StatCallback) {
		callback = reinterpret_cast<void*>(static_cast<StatCallbackFn>(EquipStatCallback));
	}
	return g_statCallbackInitOriginal(unit, flags, callback, arg);
}

using ItemAuraTickFn = void (*)(Game* game, Unit* unit, int32_t key, int32_t skill, int32_t stateKey) noexcept;
ItemAuraTickFn g_itemAuraTickOriginal = nullptr;

// D2R's item aura tick (0x437230) at item_skillonequip's level, as PD2's 0x10345cc0 runs it; item_aura's own ticks
// and an aura whose stat is gone (the event is removed) stay D2R's
auto EquipAuraTick(Game* game, Unit* unit, int32_t key, int32_t skill, int32_t stateKey) noexcept -> void {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr || skill < 0 || StatOf(unit, StatItemAura, skill) > 0) {
		g_itemAuraTickOriginal(game, unit, key, skill, stateKey);
		return;
	}
	const int32_t level = StatOf(unit, StatSkillOnEquip, skill);
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* record = d2r::api.getSkillRecord(dataFlag, skill);
	const uint8_t* tables = DataTables(dataFlag);
	const int16_t state = record != nullptr ? At<int16_t>(record, SkAuraState) : -1;
	if (level <= 0 || state < 0 || tables == nullptr || static_cast<uint64_t>(state) >= At<uint64_t>(tables, TablesStateCount) ||
	    d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) != 0) {
		g_itemAuraTickOriginal(game, unit, key, skill, stateKey);  // finds no item_aura level: removes the event
		if (skill == SkillPhasing) {
			SetPhasing(unit, level > 0);
		}
		return;
	}
	const auto stateKeyOf = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey);
	const auto setStateKey = d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetUnitStateKey);
	const auto setFlag = d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(SetUnitFlag);
	const int32_t savedKey = stateKeyOf(unit);
	setStateKey(unit, stateKey);
	// PD2's 0x10345cc0 casts nothing while the unit charges, and Armageddon SelfAura only while it stands, walks or
	// runs (modes 1, 2, 3 and town 5, 6)
	const void* currentSkill = d2r::Engine<void* (*)(Unit*) noexcept>(CurrentSkill)(unit);
	const int32_t current = currentSkill != nullptr ? d2r::Engine<int32_t (*)(const void*, const char*, int32_t) noexcept>(
	                                                UnitSkillId)(currentSkill, __FILE__, __LINE__)
	                                          : -1;
	const int32_t mode = At<int32_t>(unit, UnitMode);
	const bool moving = mode == 1 || mode == 2 || mode == 3 || mode == 5 || mode == 6;
	const bool cast = current != SkillCharge && current != SkillMonCharge && (skill != SkillArmageddonSelfAura || moving);
	if (cast) {
		setFlag(unit, UnitFlagItemAuraCast, 1);
		d2r::Engine<uint64_t (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(
			SrvSkillDispatch)(game, unit, skill, level, 1, 1, 0);
		setFlag(unit, UnitFlagItemAuraCast, 0);
	}
	const uint32_t flags = At<uint8_t>(record, SkFlags24);
	int32_t next = -1;
	if ((flags & At<uint32_t>(reinterpret_cast<const void*>(d2r::imageBase), static_cast<int>(SkillTickMaskB))) != 0 ||
	    (flags & At<uint32_t>(reinterpret_cast<const void*>(d2r::imageBase), static_cast<int>(SkillTickMaskA))) != 0) {
		const int32_t calc = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(record, SkPerDelayCalc), skill, level);
		const int32_t delay = calc > 5 ? calc : 5;
		const int32_t frame = At<int32_t>(game, GameFrame) - 1 + delay;
		next = frame - frame % delay + 1;
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(RemoveUnitEvent)(game, unit, UnitEventItemAura, key);
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(AddUnitEvent)(
			game, unit, UnitEventItemAura, next, key, skill, stateKey);
	}
	setStateKey(unit, savedKey);
	if (skill == SkillPhasing) {
		SetPhasing(unit, true);
	}
	const uint32_t n = ++g_equipTicks;
	if (n <= 20 || n % 100 == 0) {
		plog::Info("skill on equip tick: skill %d level %d, state %d, next frame %d, %s, %u so far", skill, level, state,
		           next, cast ? "cast" : "held", n);
	}
}

}  // namespace

auto InstallItemRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	static constexpr uint8_t Empty[8] = {};
	using PropertyFn = int32_t (*)(uint8_t, uint32_t, uint64_t, Unit*, const void*, uint8_t, uint16_t, uint16_t,
	                               int32_t, uint32_t, uint32_t, uint64_t) noexcept;
	const bool property = PropertySlot < PropertyFuncSlots &&
	                      context->PatchWriteU64(PropertyFuncTable + static_cast<uint64_t>(PropertySlot) * 8, Empty,
	                                             sizeof(Empty),
	                                             reinterpret_cast<uint64_t>(static_cast<PropertyFn>(PropSorcSkillRandCtc)));
	const bool hook = context->InstallInlineHook(SrvSkillDispatch, SrvSkillDispatchEntry, sizeof(SrvSkillDispatchEntry),
	                                             SkillDispatch, &g_dispatchOriginal);
	uint8_t critPatch[28];
	WeaponCritPatch(critPatch);
	const bool weaponStrike = context->PatchBytes(WeaponCritBlock, WeaponCritBlockBytes, sizeof(WeaponCritBlockBytes),
	                                              critPatch, sizeof(critPatch));
	const bool missileStrike = context->InstallInlineHook(MissileCritRoll, MissileCritRollEntry,
	                                                      sizeof(MissileCritRollEntry), MissileStrike, &g_missileRollOriginal);
	const bool baseStat = context->InstallInlineHook(StatListBaseStat, StatListBaseStatEntry,
	                                                 sizeof(StatListBaseStatEntry), BaseStat, &g_baseStatOriginal);
	const bool lifeCap = context->InstallInlineHook(AddLife, AddLifeEntry, sizeof(AddLifeEntry),
	                                                CappedAddLife, &g_addLifeOriginal);
	// The slot holds whatever the image or D2RCore put there; PD2's blow replaces it whole, so it is not kept
	const uint64_t blowSlot = *reinterpret_cast<const uint64_t*>(d2r::imageBase + CrushingBlowApplySlot);
	using BlowFn = int32_t (*)(Game*, Unit*, Unit*, d2r::CrushingBlowArgs*) noexcept;
	const bool blow = blowSlot != 0 && context->PatchWriteU64(CrushingBlowApplySlot, &blowSlot, sizeof(blowSlot),
	                                                          reinterpret_cast<uint64_t>(static_cast<BlowFn>(CrushingBlow)));
	const bool curses = context->InstallInlineHook(ApplyCurse, ApplyCurseEntry, sizeof(ApplyCurseEntry), CurseApply,
	                                               &g_applyCurseOriginal);
	// Both or neither: a callback without the tick would start item_skillonequip's aura and stop it at its first tick
	const bool tick = context->InstallInlineHook(ItemAuraTick, ItemAuraTickEntry, sizeof(ItemAuraTickEntry),
	                                             EquipAuraTick, &g_itemAuraTickOriginal);
	const bool equip = tick && context->InstallInlineHook(StatCallbackInit, StatCallbackInitEntry,
	                                                      sizeof(StatCallbackInitEntry), EquipStatCallbackInit,
	                                                      &g_statCallbackInitOriginal);
	std::snprintf(report, reportSize, "property func %d ormus%s; dospellcast skill on cast%s; weapon strike%s; missile strike%s; "
	              "op bases%s; life steal cap%s; crushing blow%s; curses%s; skill on equip%s", PropertySlot,
	              property ? "" : " FAILED (slot not empty)", hook ? "" : " FAILED (bytes differ)",
	              weaponStrike ? "" : " FAILED (bytes differ)", missileStrike ? "" : " FAILED (bytes differ)",
	              baseStat ? "" : " FAILED (bytes differ)", lifeCap ? "" : " FAILED (bytes differ)",
	              blow ? "" : " FAILED (slot write)", curses ? "" : " FAILED (bytes differ)",
	              equip ? "" : " FAILED (bytes differ)");
	return (property ? 1 : 0) + (hook ? 1 : 0) + (weaponStrike ? 1 : 0) + (missileStrike ? 1 : 0) + (baseStat ? 1 : 0) +
	       (lifeCap ? 1 : 0) + (blow ? 1 : 0) + (curses ? 1 : 0) + (equip ? 1 : 0);
}

auto ItemRuleCount() noexcept -> int {
	return 9;
}

auto ItemRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size,
	              "ormus rolls %u, spell casts %u, skill on cast rolls %u, procs %u, strike rolls %u, crits %u, "
	              "deadly strikes %u, equipped_eth reads %u, life steal cap checks %u (blocked %u), crushing blows %u, "
	              "curses %u, resisted %u, skill on equip changes %u, ticks %u, phasing %u",
	              g_rolls.load(), g_raises.load(), g_procRolls.load(), g_procs.load(), g_strikeRolls.load(), g_crits.load(),
	              g_deadlyStrikes.load(), g_ethReads.load(), g_capChecks.load(), g_capBlocks.load(), g_blows.load(),
	              g_curses.load(), g_curseResisted.load(), g_equipCallbacks.load(), g_equipTicks.load(), g_phasing.load());
}
