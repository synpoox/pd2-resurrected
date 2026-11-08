// PD2's item rules for Ormus' Robes (docs/research/p12-ormus-robes.md):
//
// - properties func 25 (`sorc-skill-rand-ctc`, ProjectDiablo.dll 0x103a12a0) in D2R's free property slot 26: one
//   random skill of PD2's sorceress list gets +param, and the item casts that skill on cast (item_skilloncast) at
//   chance min and level max, both adjusted by the skill as PD2 does.
//   The Cast Triggers companion fires item_skilloncast (IR2, patches/030-cast-triggers.json).
// - curses (docs/research/ir4-curses.md): max curses (a target holds up to the caster's max_curses + 1 curses) and
//   curse resistance (curse_effectiveness on the target scales the curse's stats), around D2R's ApplyCurse; PD2's
//   gate (#264): resisted-to-0 cancels, stat 109 cuts the duration (cap 75 on a player), attract no longer refuses.
//   Dark Pact (#265) consumes a curse and explodes around the target, scaled by the curses it held.
// - skill while equipped and phasing (docs/research/ir7-equipped-items.md): item_skillonequip runs its skill as an
//   item aura (D2R's item_aura case and tick, fed stat 375), and Phasing SelfAura drops the unit collision bits.
// - critical and deadly strike (IR3, docs/research/ir3-combat-stats.md): PD2's roll and multipliers in place of
//   D2R's, in WeaponDamage's roll block (a byte patch calling WeaponStrike) and D2R's missile roll.
// - op bases, life steal cap and crushing blow (IR3): equipped_eth / missing_hp kept as base stats from the stat
//   callback, lifedrain_percentcap on D2R's add-life, PD2's blow in CrushingBlowApply's jump slot, and its roll on
//   D2RCore's event dispatch.
#include "item_rules.h"

#include "pd2_engine.h"
#include "log.h"

#include <D2RLPlugin/threads.h>

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

std::atomic<uint32_t> g_rolls {0};

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

constexpr int32_t ItemType2han = 120, ItemType1han = 122;  // merged itemtypes rows (PD2's 114, 116)

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
// part (damage +0x18) times 200 + item_crit_multiplier or 150 + item_ds_multiplier percent, each plus the stat's
// per-hand sum (0x103047f0: Javelin and Spear Mastery's crit multiplier sits on the spea layer), flag 0x2000.
// a5 keeps D2R's gate on the mastery's crit (D2R adds it only when a5 is 0).
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
		const uint32_t stat = s.kind == 1 ? StatCritMultiplier : StatDsMultiplier;
		multiplier = (s.kind == 1 ? 200 : 150) + Stat(attacker, stat) + HandStatSum(attacker, stat);
		const int64_t raised = static_cast<int64_t>(before) * (multiplier > 0 ? multiplier : 0) / 100;
		*reinterpret_cast<uint32_t*>(damage + 0x18) = static_cast<uint32_t>(raised < 0x7fffffff ? raised : 0x7fffffff);
		*reinterpret_cast<uint16_t*>(damage + 4) |= 0x2000;
	}
	LogStrike("weapon", attacker, s, before, At<uint32_t>(damage, 0x18), multiplier);
}

// D2R's missile roll (MissileCritRoll) with PD2's chances; the hit doubles, as in PD2, whose live missile fill
// (0x102fe730) rolls the same way and whose multiplier for missiles (0x103045a0) nothing calls
// (docs/research/ir3-combat-stats.md)
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

constexpr uint32_t StatDurability = 72;

std::atomic<uint32_t> g_ethReads {0};
std::atomic<uint32_t> g_missingHpRefreshes {0};
const D2RL::PluginContext* g_context = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

// UI thread: the same value on the client's copy of the player, whose tooltips read it (the server never sends it)
void MirrorOpBase(const D2RL::PluginContext*, void* user) noexcept {
	const auto packed = reinterpret_cast<uintptr_t>(user);
	Unit* client = d2r::Engine<Unit* (*)(int32_t, int32_t) noexcept>(d2r::rva::ClientFindUnit)(
		static_cast<int32_t>(packed >> 32), 0);
	if (client != nullptr) {
		d2r::Engine<void (*)(Unit*, uint32_t, int32_t, uint32_t) noexcept>(d2r::rva::SetUnitStat)(
			client, static_cast<uint32_t>((packed >> 16) & 0xffff), static_cast<int32_t>(packed & 0xffff), 0);
	}
}

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

constexpr uint32_t StatDmgPerEth = 537;        // item_dmgpercent_pereth (PD2's 484), op 4 on equipped_eth
constexpr uint32_t StatDmgPerMissingHp = 540;  // item_dmgpercent_permissinghppercent (PD2's 487), op 4 on missing_hp
constexpr uint32_t StatMaxDamagePercent = 17;  // item_maxdamage_percent, both ops' first target

auto SetStat(Unit* unit, uint32_t stat, int32_t value) noexcept -> void {
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t, uint32_t) noexcept>(d2r::rva::SetUnitStat)(unit, stat, value, 0);
}

// D2R keeps an op's result in each stat list's full stats and recomputes it only when a stat of that list changes
// (0x2fa430 walks the changed stat's op targets), so a new op base on the owner leaves a worn item's op value as it
// was at equip time (#320). Moving the op stat by one and back on each worn item that holds it makes the stat code
// recompute its targets with the new base. PD2 needs no such step: it evaluates ops on read (0x102faa90). The op adds
// nothing from the unit's own list (`cheat stat` on the player, seen in game), so only items are walked.
// No SDK service covers stats, so this goes through SetUnitStat rather than a hook in D2RCore's stat engine.
auto RecomputeOp(Unit* unit, uint32_t opStat) noexcept -> int32_t {
	using namespace d2r::rva;
	const auto baseStat = d2r::Engine<int32_t (*)(Unit*, uint32_t, uint32_t) noexcept>(UnitBaseStat);
	int32_t lists = 0;
	auto toggle = [&](Unit* holder) noexcept {
		const int32_t base = baseStat(holder, opStat, 0);
		SetStat(holder, opStat, base + 1);
		SetStat(holder, opStat, base);
		++lists;
	};
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory)(unit, __FILE__, __LINE__);
	if (inventory != nullptr) {
		const auto bodyItem = d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(InventoryBodyItem);
		for (int32_t loc = BodyLocFirst; loc <= BodyLocLast; ++loc) {
			Unit* item = bodyItem(inventory, loc);
			if (item != nullptr && Stat(item, opStat) != 0) {  // an affix sits on the item's magic list, not its base one
				toggle(item);
			}
		}
	}
	return lists;
}

// PD2's 0x102fa640 computes equipped_eth and missing_hp where an op base is read. D2RCore's stat code reads op bases
// itself and never calls D2R's read (0x2f9b10), so core-rules keeps both as the unit's real base stats instead, and
// D2RCore's op 4 reads them: equipped_eth when the unit's summed durability changes (an item worn or taken off:
// patches/060-op-base-callbacks.json gives durability and hitpoints fCallback), missing_hp when life or max life does.
// The server never sends either to the client, so a player's values are mirrored onto its client unit for tooltips.
// PD2's third case, inc_splash_radius + missing_hp x inc_splash_radius_permissinghp (at most 40), is only reached
// with inc_splash_radius as an op base, which no stat has: not ported (docs/research/ir3-combat-stats.md).
// shortcut: an ethereal item without durability (a throwing stack) raises no callback; the count follows at the next
// item change. Upgrade with an equip hook if one is found.
auto RefreshOpBase(Unit* unit, uint32_t stat) noexcept -> void {
	const int32_t type = At<int32_t>(unit, d2r::rva::UnitType);
	if (type != 0 && type != 1) {
		return;
	}
	int32_t value = 0;
	if (stat == StatEquippedEth) {
		value = WornEthereal(unit);
	} else {
		const int64_t life = Stat(unit, StatLife), maxLife = Stat(unit, StatMaxLife);
		value = maxLife > 0 && life < maxLife ? static_cast<int32_t>((maxLife - life) * 100 / maxLife) : 0;
	}
	if (value == Stat(unit, stat)) {
		return;
	}
	SetStat(unit, stat, value);
	const int32_t edBefore = Stat(unit, StatMaxDamagePercent);
	const int32_t lists = RecomputeOp(unit, stat == StatEquippedEth ? StatDmgPerEth : StatDmgPerMissingHp);
	const int32_t edAfter = Stat(unit, StatMaxDamagePercent);
	if (type == 0 && g_threads != nullptr) {
		const uintptr_t packed = static_cast<uintptr_t>(At<uint32_t>(unit, d2r::rva::UnitIdField)) << 32 |
		                         static_cast<uintptr_t>(stat & 0xffff) << 16 | static_cast<uintptr_t>(value & 0xffff);
		g_threads->runOnUiThread(g_context, MirrorOpBase, reinterpret_cast<void*>(packed));
	}
	if (stat == StatEquippedEth) {
		const uint32_t n = ++g_ethReads;
		if (n <= 20 || n % 200 == 0) {
			plog::Info("equipped_eth: unit type %d wears %d ethereal, %d op lists recomputed, item_maxdamage_percent %d "
			           "-> %d, %u changes so far", type, value, lists, edBefore, edAfter, n);
		}
	} else if (lists != 0 && type == 0) {
		const uint32_t n = ++g_missingHpRefreshes;
		if (n <= 20 || n % 200 == 0) {
			plog::Info("missing_hp: player at %d%%, %d op lists recomputed, item_maxdamage_percent %d -> %d, %u so far",
			           value, lists, edBefore, edAfter, n);
		}
	}
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

constexpr int32_t SkillSmite = 97;

// PD2's effectiveness: 1, plus Smite's calc4 / 100 while the unit's current skill is Smite, at the unit skill's hard
// points (1.13c's skill +0x28)
auto BlowEffectiveness(Game* game, Unit* unit) noexcept -> double {
	using namespace d2r::rva;
	void* current = d2r::Engine<void* (*)(Unit*) noexcept>(CurrentSkill)(unit);
	if (current == nullptr ||
	    d2r::Engine<int32_t (*)(const void*, const char*, int32_t) noexcept>(UnitSkillId)(current, __FILE__, __LINE__) !=
	        SkillSmite) {
		return 1.0;
	}
	const uint8_t* record = d2r::api.getSkillRecord(d2r::DataFlag(game), SkillSmite);
	const int32_t level = d2r::Engine<int32_t (*)(Unit*, void*, int32_t) noexcept>(UnitSkillLevel)(unit, current, 0);
	return record != nullptr ? 1.0 + d2r::EvalCalc(game, unit, record, SkCalc4, SkillSmite, level) / 100.0 : 1.0;
}

// PD2's blow: life / (divisor / (efficiency / 100 + effectiveness)) (divisor x1.5 for a missile), efficiency =
// item_crushingblow_efficiency + its per-hand sum (Two Hand Mastery's 2han layer), less the target's damageresist
// percent, nothing at 100 or more; then the kill flag and the overlay. D2R's event 16 and Smite's roll
// (skill_functions.cpp) both call it.
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
	const int32_t efficiency = Stat(unit, StatCbEfficiency) + HandStatSum(unit, StatCbEfficiency);
	const double effectiveness = BlowEffectiveness(game, unit);
	const double life = static_cast<uint32_t>(Stat(target, StatLife));
	double blow = life / (divisor / (efficiency / 100.0 + effectiveness));
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
		plog::Info("crushing blow: unit type %d on unit type %d class %d%s: divisor %.2f, efficiency %d, "
		           "effectiveness %.2f, resist %d, life %.0f -> %d (1/256), %u so far", At<int32_t>(unit, UnitType),
		           At<int32_t>(target, UnitType), At<int32_t>(target, UnitClassId), args->missile != 0 ? " (missile)" : "",
		           divisor, efficiency, effectiveness, resist, life, after, n);
	}
	return 1;
}

// PD2's event 16 roll on a hit: item_crushingblow plus its per-hand sum, % 100 on the unit's seed. D2R would roll it
// from the item's event node (itemeventfunc 16), but that node runs D2RCore's own copy of the event table: D2R's blow
// (a quarter of a monster's life, less its damage resist), never D2R's event 16 or CrushingBlowApply's slot (seen in
// game, #446). core-rules hides those nodes from D2RCore's event dispatch for the call (no SDK service reaches it) and
// rolls PD2's blow itself after it.
// Smite rolls its own (skill_functions.cpp, srvdo 150) with calc3 added, so a Smite hit gets no second roll.
constexpr uint32_t StatCrushingBlow = 136;
constexpr int32_t EventMeleeDamage = 5, EventMissileDamage = 6;
constexpr int MaxHiddenNodes = 16;  // a unit wears 12 items; more only guards a corrupt chain
constexpr uint16_t NoEvent = 0xffff;  // no event raises it

std::atomic<uint32_t> g_blowRolls {0};

using DispatchFn = void (*)(Game*, int32_t, Unit*, Unit*, void*, void*) noexcept;
DispatchFn g_dispatchOriginal = nullptr;

auto DispatchEvent(Game* game, int32_t event, Unit* unit, Unit* target, void* damage, void* list) noexcept -> void {
	using namespace d2r::rva;
	if ((event != EventMeleeDamage && event != EventMissileDamage) || game == nullptr || unit == nullptr ||
	    target == nullptr || damage == nullptr) {
		g_dispatchOriginal(game, event, unit, target, damage, list);
		return;
	}
	uint8_t* hidden[MaxHiddenNodes];
	int count = 0;
	for (auto* node = At<uint8_t*>(unit, UnitEventNodes); node != nullptr && count < MaxHiddenNodes;
	     node = At<uint8_t*>(node, EventNodeNext)) {
		if (At<uint16_t>(node, EventNodeEvent) == event && At<int32_t>(node, EventNodeStat) == static_cast<int32_t>(StatCrushingBlow)) {
			*reinterpret_cast<uint16_t*>(node + EventNodeEvent) = NoEvent;
			hidden[count++] = node;
		}
	}
	g_dispatchOriginal(game, event, unit, target, damage, list);
	for (int i = 0; i < count; ++i) {
		*reinterpret_cast<uint16_t*>(hidden[i] + EventNodeEvent) = static_cast<uint16_t>(event);
	}
	const int32_t chance = Stat(unit, StatCrushingBlow) + HandStatSum(unit, StatCrushingBlow);
	if (chance <= 0 || d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(target) != 0) {
		return;
	}
	void* current = d2r::Engine<void* (*)(Unit*) noexcept>(CurrentSkill)(unit);
	if (current != nullptr &&
	    d2r::Engine<int32_t (*)(const void*, const char*, int32_t) noexcept>(UnitSkillId)(current, __FILE__, __LINE__) ==
	        SkillSmite) {
		return;
	}
	const int32_t roll = static_cast<int32_t>(d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit), 100));
	const uint32_t n = ++g_blowRolls;
	if (At<int32_t>(unit, UnitType) == 0 || n <= 20) {
		plog::Info("crushing blow roll: unit type %d event %d, chance %d roll %d, %d item nodes hidden, %u so far",
		           At<int32_t>(unit, UnitType), event, chance, roll, count, n);
	}
	if (roll < chance) {
		d2r::CrushingBlowArgs args {damage, {4, 10, 10}, static_cast<uint8_t>(event == EventMissileDamage ? 1 : 0)};
		CrushingBlow(game, unit, target, &args);
	}
}

// --- Curses: curse resistance and max curses (docs/research/ir4-curses.md) ---------------------------------------
constexpr int32_t StatCurseSlots = 374;          // curse_slots, merged id (PD2 190): the slot count a curse was cast with
constexpr int32_t StatMaxCurses = 421;           // max_curses (PD2 368)
constexpr int32_t StatCurseResistance = 557;     // curse_effectiveness, shown as Curse Resistance (PD2 504)
constexpr int32_t StatCurseDuration = 109;      // curse_resistance: cuts a curse's duration (same id merged and PD2)
constexpr int32_t StateAttract = 57;             // attract (merged id; D2R's NewStateList tests 0x39)
constexpr int32_t CurseResistanceCap = 75;
constexpr int MaxCurseLists = 64;  // a unit holds a handful; more only stops a corrupt chain from spinning

std::atomic<uint32_t> g_curses {0};
std::atomic<uint32_t> g_curseResisted {0};
std::atomic<uint32_t> g_curseGate {0};

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

auto GateLog(uint32_t n) noexcept -> bool {
	return n <= 20 || n % 100 == 0;
}

// PD2's curse gate (0x1036d7d0) ahead of D2R's: a curse whose aurastat1 resists to 0 is cancelled, and stat 109 cuts
// the duration by PD2's formula (at most 75 on a player), a duration of 0 cancelling it. Returns false to cancel;
// `frames` is the duration to hand D2R, `duration` the target's stat 109.
auto CurseGate(const d2r::CurseParams* params, Unit* target, int32_t& frames, int32_t& duration) noexcept -> bool {
	const bool player = At<int32_t>(target, d2r::rva::UnitType) == 0;
	const auto capped = [player](int32_t r) { return player && r > CurseResistanceCap ? CurseResistanceCap : r; };
	const int32_t resistance = StatOf(target, StatCurseResistance);
	if (resistance != 0 && params->stats[0] >= 0) {
		const int32_t base = d2r::Engine<int32_t (*)(Game*, Unit*, int32_t, int32_t) noexcept>(d2r::rva::CurseStatValue)(
			params->game, target, params->stats[0], params->values[0]);
		if (base != 0 && Resisted(base, capped(resistance)) == 0) {
			if (const uint32_t n = ++g_curseGate; GateLog(n)) {
				plog::Info("curse gate: state %d cancelled on %s, aurastat1 stat %d %d resists to 0 at %d (used %d), "
				           "%u so far", params->state, player ? "player" : "monster", params->stats[0], base, resistance,
				           capped(resistance), n);
			}
			return false;
		}
	}
	frames = params->frames;
	duration = StatOf(target, StatCurseDuration);
	if (duration == 0 || frames <= 0) {
		return true;
	}
	frames = Resisted(params->frames, capped(duration));
	if (const uint32_t n = ++g_curseGate; GateLog(n)) {
		plog::Info("curse gate: state %d on %s, curse_resistance %d (used %d): %d -> %d frames%s, %u so far",
		           params->state, player ? "player" : "monster", duration, capped(duration), params->frames, frames,
		           frames <= 0 ? ", cancelled" : "", n);
	}
	return frames > 0;
}

using ApplyCurseFn = int64_t (*)(Unit* target, d2r::CurseParams* params) noexcept;
ApplyCurseFn g_applyCurseOriginal = nullptr;

// D2R's NewStateList (0x433d20, its entry D2RCore's, so not hookable here) refuses a curse at stat 109 >= 100, cuts
// the duration by it uncapped, and refuses a curse on a unit with attract. PD2 does neither, so the call runs with
// the target's stat 109 zeroed on its base layer (the gate already cut `frames`) and its attract bit off. Both come
// back before this returns; nothing else runs in between (one game thread).
auto ApplyUngated(Unit* target, d2r::CurseParams* params, uint32_t dataFlag, int32_t frames, int32_t duration) noexcept
	-> int64_t {
	using namespace d2r::rva;
	const auto setStat = d2r::Engine<void (*)(Unit*, uint32_t, int32_t, uint32_t) noexcept>(SetUnitStat);
	void* owner = At<void*>(target, UnitListOwner);
	int32_t base = 0;
	bool zeroed = false;
	if (duration != 0 && owner != nullptr) {
		base = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue)(dataFlag, owner,
		                                                                                          StatCurseDuration, 0);
		setStat(target, StatCurseDuration, base - duration, 0);
		zeroed = StatOf(target, StatCurseDuration) == 0;
		if (!zeroed) {  // stat 109 not where it was looked for: D2R's own rule applies, on the uncut duration
			setStat(target, StatCurseDuration, base, 0);
			plog::Info("curse gate: stat 109 %d did not clear (base %d), D2R's duration rule applies", duration, base);
		}
	}
	// The bit UnitHasState reads (SetStateGfxFlag writes another array)
	const auto hasState = d2r::Engine<uint32_t (*)(Unit*, int32_t) noexcept>(UnitHasState);
	auto* word = owner != nullptr && At<int32_t>(owner, ListOwnerMark) < 0
	                 ? reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(owner) + ListOwnerStates + (StateAttract >> 5) * 4)
	                 : nullptr;
	const uint32_t bit = 1u << (StateAttract & 31);
	const bool attracted = word != nullptr && hasState(target, StateAttract) != 0;
	if (attracted) {
		*word &= ~bit;
		if (hasState(target, StateAttract) != 0) {
			plog::Info("curse gate: the attract bit did not clear");
		}
	}
	const int32_t asked = params->frames;
	params->frames = zeroed ? frames : asked;  // the srvdo's area walk reuses params for its next target
	const int64_t applied = g_applyCurseOriginal(target, params);
	params->frames = asked;
	if (zeroed) {
		setStat(target, StatCurseDuration, base, 0);
		if (const int32_t back = StatOf(target, StatCurseDuration); back != duration) {
			plog::Info("curse gate: stat 109 came back %d, not %d (base %d)", back, duration, base);
		}
	}
	// A full target may have lost the attract curse itself to this one: then its bit stays off
	if (attracted && d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(target, StateAttract) != nullptr) {
		*word |= bit;
	}
	if (attracted) {
		if (const uint32_t n = ++g_curseGate; GateLog(n)) {
			plog::Info("curse gate: state %d on an attracted %s, %s, %u so far", params->state,
			           At<int32_t>(target, UnitType) == 0 ? "player" : "monster", applied != 0 ? "landed" : "refused", n);
		}
	}
	return applied;
}

// --- Dark Pact (#265, docs/research/ir4-curses.md): PD2's state list creator 0x1036d7d0, state 196 ----------------
// PD2 refuses a monster D2R's NewStateList also refuses (below), then consumes the first curse in the target's chain
// that is not the oldest timed one (so a lone timed curse stays), then
// hits every unit within calc1 + curses - 1 of the target with the skill's magic damage times 1 + 1.1 * (curses - 1)
// (D2Game 0x9f210's roll, area search 0xa0c70 with mask 0x8583 and callback 0xa1210), then puts dark_pact on the
// target past the max-curses rule. No curse on the target: nothing. PD2 skips its curse gate for this state.
constexpr int32_t StateDarkPact = 242;  // dark_pact, merged id (PD2 196)
constexpr int ListState = 0x20;         // int32 the list's state (rva::SetListState writes it)
constexpr uint32_t DarkPactHitFlags = 1;
constexpr uint32_t DarkPactResultFlags = 5;

std::atomic<uint32_t> g_darkPacts {0};

// PD2's early-out in 0x1036d7d0, also D2R's NewStateList's first checks (0x433d82-0x433f16): a monster target is
// refused when its monstats row is missing or flagged npc (flags bit 8: byte +0x3d & the bit-mask table's entry 0),
// or its monstats2 row (monstats +0x4c, table +0xf98, 0x128 a row) lacks revive (flags bit 9: byte +5 & entry 1).
// Dark Pact must ask first: its explosion comes before NewStateList.
constexpr int MonStats2Index = 0x4c;                      // int16 monstats -> monstats2 row
constexpr int TablesMonStats2 = 0xf98, TablesMonStats2Count = 0xfa0;
constexpr int MonStats2Size = 0x128, MonStats2Flags1 = 0x5;  // byte: flags bits 8-15
constexpr uint64_t BitMasks = 0x1d996d0;                   // uint32[32]: 1 << i (MeleeRollFlagMask is entry 0)

auto StateRefused(uint32_t dataFlag, Unit* target) noexcept -> bool {
	using namespace d2r::rva;
	if (At<int32_t>(target, UnitType) != UnitMonster) {
		return false;
	}
	const auto* masks = reinterpret_cast<const uint8_t*>(d2r::imageBase + BitMasks);
	const uint8_t* mon = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
		dataFlag, At<int32_t>(target, UnitClassId));
	if (mon == nullptr || (At<uint8_t>(mon, MonFlags + 1) & masks[0]) != 0) {
		return true;
	}
	const int64_t row = At<int16_t>(mon, MonStats2Index);
	const uint8_t* tables = DataTables(dataFlag);
	if (tables == nullptr || row < 0 || static_cast<uint64_t>(row) >= At<uint64_t>(tables, TablesMonStats2Count)) {
		return true;
	}
	const uint8_t* mon2 = At<const uint8_t*>(tables, TablesMonStats2) + row * MonStats2Size;
	return (At<uint8_t>(mon2, MonStats2Flags1) & masks[4]) == 0;
}

using AreaFn = void (*)(Game*, Unit*, d2r::AreaCenter*, int32_t, uint32_t, int32_t (*)(d2r::AreaContext*, Unit*), void*,
                        int32_t) noexcept;  // rva::AreaUnits

struct DarkPactHit {
	d2r::LeapKnockback* rolled;  // one roll for every unit in range, as PD2's callback copies one block
	int64_t victims;
};

// One unit in range (1.13c D2Game 0xa1210): a copy of the roll through the area apply
auto DarkPactOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	auto* hit = static_cast<DarkPactHit*>(area->arg);
	d2r::LeapKnockback damage;
	d2r::CopyRolled(*hit->rolled, damage);
	damage.hitFlags = DarkPactHitFlags;
	damage.resultFlags = DarkPactResultFlags;
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*) noexcept>(d2r::rva::AreaApply)(static_cast<Game*>(area->game),
	                                                                               area->unit, victim, &damage);
	++hit->victims;
	d2r::ReleaseDamage(damage);
	return 1;
}

// PD2's multiplier: the magic damage, as an unsigned value, times (curses - 1.0) * 1.1 + 1.0, truncated
auto DarkPactDamage(int32_t magic, int curses) noexcept -> int32_t {
	const double v = static_cast<double>(static_cast<uint32_t>(magic)) * ((curses - 1.0) * 1.1 + 1.0);
	return v >= 4294967295.0 ? -1 : static_cast<int32_t>(static_cast<uint32_t>(v));
}

auto ApplyDarkPact(Unit* target, d2r::CurseParams* params, uint32_t dataFlag) noexcept -> int64_t {
	using namespace d2r::rva;
	Game* game = params->game;
	Unit* caster = params->caster;
	uint8_t* owner = At<uint8_t*>(target, UnitListOwner);
	if (owner == nullptr || At<int32_t>(owner, ListOwnerMark) >= 0) {
		return 0;
	}
	if (StateRefused(dataFlag, target)) {
		if (const uint32_t n = ++g_darkPacts; n <= 20 || n % 100 == 0) {
			plog::Info("dark pact: monster class %d refused (npc or not revivable), nothing happens, %u so far",
			           At<int32_t>(target, UnitClassId), n);
		}
		return 0;
	}
	const auto listStat = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue);
	const auto setListStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	const auto stateList = d2r::Engine<uint8_t* (*)(Unit*, int32_t) noexcept>(UnitStateList);
	// PD2's walk: count the curses, keep the most slots any was cast with, find the oldest timed one
	const int32_t own = StatOf(caster, StatMaxCurses) + 1;
	int32_t slots = own;
	int count = 0;
	uint8_t* oldest = nullptr;
	int guard = 0;
	for (uint8_t* list = At<uint8_t*>(owner, ListOwnerFirst); list != nullptr && guard < MaxCurseLists;
	     list = At<uint8_t*>(list, ListNext), ++guard) {
		if ((At<uint32_t>(list, ListFlags) & ListFlagCurse) == 0) {
			continue;
		}
		++count;
		const int32_t kept = listStat(dataFlag, list, StatCurseSlots, 0);
		if (kept != 0 && kept < slots) {
			setListStat(dataFlag, list, StatCurseSlots, 0, 0);
		} else if (kept > slots) {
			slots = kept;
		}
		if ((At<uint32_t>(list, ListFlags) & ListFlagTimed) != 0) {
			oldest = list;
		}
	}
	// The first curse that is not the oldest timed one goes (PD2's 0x1030c530), state bit off as RemoveSkillState does
	int32_t consumed = -1;
	guard = 0;
	for (uint8_t* list = At<uint8_t*>(owner, ListOwnerFirst); list != nullptr && guard < MaxCurseLists;
	     list = At<uint8_t*>(list, ListNext), ++guard) {
		if ((At<uint32_t>(list, ListFlags) & ListFlagCurse) != 0 && list != oldest) {
			consumed = At<int32_t>(list, ListState);
			d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateBit)(target, consumed, 0);
			d2r::Engine<void (*)(Unit*, void*) noexcept>(DetachStatList)(target, list);
			d2r::Engine<void (*)(uint32_t, void*) noexcept>(FreeStatList)(dataFlag, list);
			break;
		}
	}
	const uint32_t n = ++g_darkPacts;
	const bool log = n <= 20 || n % 100 == 0;
	if (count < 1) {
		if (log) {
			plog::Info("dark pact: no curse on the target, nothing happens, %u so far", n);
		}
		return 0;
	}
	// The follow-up: the skill's magic roll, scaled, around the target
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, params->skill);
	int32_t x = 0, y = 0;
	int32_t radius = 0, base = 0, scaled = 0;
	int64_t victims = 0;
	if (skill != nullptr && caster != nullptr && d2r::UnitXY(target, &x, &y)) {
		radius = d2r::EvalCalc(game, caster, skill, SkCalc1, params->skill, params->level) + count - 1;
		d2r::LeapKnockback rolled;
		d2r::Engine<void (*)(Unit*, void*, int32_t, int32_t) noexcept>(ElementalDamage)(caster, &rolled, params->skill,
		                                                                                params->level);
		auto& magic = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&rolled) + DmgMagic);
		base = magic;
		magic = scaled = DarkPactDamage(magic, count);
		DarkPactHit hit {&rolled, 0};
		d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(target), x, y};
		d2r::Engine<AreaFn>(AreaUnits)(game, caster, &center, radius, AreaUnitMask, DarkPactOne, &hit, 0);
		victims = hit.victims;
		d2r::ReleaseDamage(rolled);
	}
	// dark_pact itself, past the max-curses rule: NewStateList sees only an older dark_pact list, if any
	uint8_t* same = stateList(target, StateDarkPact);
	uint8_t* hidden[MaxCurseLists];
	int hiddenCount = 0;
	owner = At<uint8_t*>(target, UnitListOwner);
	guard = 0;
	for (uint8_t* list = owner != nullptr ? At<uint8_t*>(owner, ListOwnerFirst) : nullptr;
	     list != nullptr && guard < MaxCurseLists; list = At<uint8_t*>(list, ListNext), ++guard) {
		if ((At<uint32_t>(list, ListFlags) & ListFlagCurse) != 0 && list != same) {
			*reinterpret_cast<uint32_t*>(list + ListFlags) &= ~ListFlagCurse;
			hidden[hiddenCount++] = list;
		}
	}
	const int64_t applied = ApplyUngated(target, params, dataFlag, params->frames, StatOf(target, StatCurseDuration));
	for (int i = 0; i < hiddenCount; ++i) {
		*reinterpret_cast<uint32_t*>(hidden[i] + ListFlags) |= ListFlagCurse;
	}
	uint8_t* list = applied != 0 ? stateList(target, StateDarkPact) : nullptr;
	if (list != nullptr && list != same && own > StatOf(target, StatCurseSlots)) {
		setListStat(dataFlag, list, StatCurseSlots, own, 0);
	}
	if (log) {
		plog::Info("dark pact: skill %d level %d, %d curse(s) on the target, consumed state %d; follow-up radius %d, "
		           "magic %d -> %d (1/256), %lld unit(s) hit; dark_pact %s, %u so far",
		           params->skill, params->level, count, consumed, radius, base, scaled,
		           static_cast<long long>(victims), list != nullptr ? "on" : "refused", n);
	}
	return applied;
}

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
	if (state == StateDarkPact) {
		return ApplyDarkPact(target, params, dataFlag);
	}
	int32_t frames = 0, duration = 0;
	if (!CurseGate(params, target, frames, duration)) {
		return 0;
	}
	const auto stateList = d2r::Engine<uint8_t* (*)(Unit*, int32_t) noexcept>(UnitStateList);
	uint8_t* before = stateList(target, state);
	uint8_t* hidden[MaxCurseLists];
	int hiddenCount = 0;
	const int32_t slots = MakeRoom(dataFlag, params->caster, target, state, hidden, hiddenCount);
	const int64_t applied = ApplyUngated(target, params, dataFlag, frames, duration);
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

// The game the last durability callback came from; a session change discards the queued recounts with it
Game* g_ethGame = nullptr;

void RecountEthereal(const D2RL::PluginContext*, void* user) noexcept {
	const auto key = reinterpret_cast<uintptr_t>(user);
	Unit* unit = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(d2r::rva::FindUnit)(
		g_ethGame, static_cast<int32_t>(key >> 32), static_cast<uint32_t>(key));
	if (unit != nullptr) {
		RefreshOpBase(unit, StatEquippedEth);
	}
}

using StatCallbackFn = uint64_t (*)(Game* game, Unit* unit, Unit* item, uint64_t statLayer, int32_t oldValue,
                                    int32_t newValue) noexcept;

// Every unit's stat callback (see StatCallbackInit): item_skillonequip goes to D2R's as item_aura, same layer. The
// stat id is r9's high dword, the layer its low one (seen in game: 375/624 arrives as 0x17700000270)
auto EquipStatCallback(Game* game, Unit* unit, Unit* item, uint64_t statLayer, int32_t oldValue, int32_t newValue) noexcept
	-> uint64_t {
	const auto original = d2r::Engine<StatCallbackFn>(d2r::rva::StatCallback);
	const uint64_t stat = statLayer >> 32;
	if (unit != nullptr && stat == StatDurability) {  // fCallback only for the op bases: D2R has no case
		// Counted at the next game update: while the callback runs, an item being taken off still sits in its slot
		g_ethGame = game;
		const uintptr_t key = static_cast<uintptr_t>(At<uint32_t>(unit, d2r::rva::UnitType)) << 32 |
		                      At<uint32_t>(unit, d2r::rva::UnitIdField);
		if (g_threads == nullptr ||
		    g_threads->runOnGameThread(g_context, RecountEthereal, reinterpret_cast<void*>(key)) !=
		        D2RL::Threads::Result::Success) {
			RefreshOpBase(unit, StatEquippedEth);
		}
		return 0;
	}
	if (unit != nullptr && stat == StatLife) {
		RefreshOpBase(unit, StatMissingHp);
		return 0;
	}
	if (stat != static_cast<uint64_t>(StatSkillOnEquip)) {
		const uint64_t result = original(game, unit, item, statLayer, oldValue, newValue);
		if (unit != nullptr && stat == StatMaxLife) {
			RefreshOpBase(unit, StatMissingHp);
		}
		return result;
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

// PD2's 0x10381720: each item-type layer of the stat that the wielded weapon is of; 2han counts only with the other
// hand empty or holding the same item, 1han only when the other hand holds another item or none
// shortcut: PD2 also counts the 1han layer for a dual-wield class's 2han weapon beside another item; no stat this sum
// is used for has a 1han layer (skills passiveitype), upgrade if one gets it.
auto HandStatSum(Unit* unit, uint32_t stat) noexcept -> int32_t {
	using namespace d2r::rva;
	Unit* weapon = unit != nullptr ? d2r::Engine<Unit* (*)(Unit*) noexcept>(UnitWeapon)(unit) : nullptr;
	const uint8_t* tables = weapon != nullptr ? ::DataTables(d2r::api.unitDataFlag(unit)) : nullptr;
	if (tables == nullptr) {
		return 0;
	}
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory)(unit, __FILE__, __LINE__);
	const auto bodyItem = d2r::Engine<Unit* (*)(void*, int32_t) noexcept>(InventoryBodyItem);
	Unit* right = inventory != nullptr ? bodyItem(inventory, 4) : nullptr;  // bodylocs 4 and 5 are the hands
	Unit* other = right == weapon && inventory != nullptr ? bodyItem(inventory, 5) : right;
	const auto isType = d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType);
	const int64_t types = At<int64_t>(tables, TablesItemTypesCount);
	int32_t sum = 0;
	for (int32_t type = 1; type < types; ++type) {
		if (isType(weapon, type) == 0) {
			continue;
		}
		const bool counted = type == ItemType2han ? other == nullptr || other == weapon
		                     : type == ItemType1han ? other != weapon : true;
		sum += counted ? StatOf(unit, static_cast<int32_t>(stat), type) : 0;
	}
	return sum;
}

auto InstallItemRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	g_context = context;
	if (context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize)) {
		g_threads = nullptr;  // the server stats still work; only the client's tooltips miss them
	}
	static constexpr uint8_t Empty[8] = {};
	using PropertyFn = int32_t (*)(uint8_t, uint32_t, uint64_t, Unit*, const void*, uint8_t, uint16_t, uint16_t,
	                               int32_t, uint32_t, uint32_t, uint64_t) noexcept;
	const bool property = PropertySlot < PropertyFuncSlots &&
	                      context->PatchWriteU64(PropertyFuncTable + static_cast<uint64_t>(PropertySlot) * 8, Empty,
	                                             sizeof(Empty),
	                                             reinterpret_cast<uint64_t>(static_cast<PropertyFn>(PropSorcSkillRandCtc)));
	uint8_t critPatch[28];
	WeaponCritPatch(critPatch);
	const bool weaponStrike = context->PatchBytes(WeaponCritBlock, WeaponCritBlockBytes, sizeof(WeaponCritBlockBytes),
	                                              critPatch, sizeof(critPatch));
	const bool missileStrike = context->InstallInlineHook(MissileCritRoll, MissileCritRollEntry,
	                                                      sizeof(MissileCritRollEntry), MissileStrike, &g_missileRollOriginal);
	// D2RCore's event dispatcher is in the slot; the wrapper calls it
	g_dispatchOriginal = *reinterpret_cast<DispatchFn const*>(d2r::imageBase + EventDispatchSlot);
	const uint64_t dispatchSlot = reinterpret_cast<uint64_t>(g_dispatchOriginal);
	const bool blowRoll = dispatchSlot != 0 &&
	                      context->PatchWriteU64(EventDispatchSlot, &dispatchSlot, sizeof(dispatchSlot),
	                                             reinterpret_cast<uint64_t>(static_cast<DispatchFn>(DispatchEvent)));
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
	std::snprintf(report, reportSize, "property func %d ormus%s; weapon strike%s; missile strike%s; "
	              "crushing blow roll%s; life steal cap%s; crushing blow%s; curses%s; skill on equip and op bases%s",
	              PropertySlot, property ? "" : " FAILED (slot not empty)",
	              weaponStrike ? "" : " FAILED (bytes differ)", missileStrike ? "" : " FAILED (bytes differ)",
	              blowRoll ? "" : " FAILED (slot write)", lifeCap ? "" : " FAILED (bytes differ)",
	              blow ? "" : " FAILED (slot write)", curses ? "" : " FAILED (bytes differ)",
	              equip ? "" : " FAILED (bytes differ)");
	return (property ? 1 : 0) + (weaponStrike ? 1 : 0) + (missileStrike ? 1 : 0) + (blowRoll ? 1 : 0) +
	       (lifeCap ? 1 : 0) + (blow ? 1 : 0) + (curses ? 1 : 0) + (equip ? 1 : 0);
}

auto ItemRuleCount() noexcept -> int {
	return 8;
}

auto ItemRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size,
	              "ormus rolls %u, strike rolls %u, crits %u, "
	              "deadly strikes %u, equipped_eth changes %u, life steal cap checks %u (blocked %u), crushing blow rolls %u, "
	              "crushing blows %u, curses %u, resisted %u, gated %u, skill on equip changes %u, ticks %u, phasing %u",
	              g_rolls.load(), g_strikeRolls.load(), g_crits.load(), g_deadlyStrikes.load(), g_ethReads.load(),
	              g_capChecks.load(), g_capBlocks.load(), g_blowRolls.load(), g_blows.load(),
	              g_curses.load(), g_curseResisted.load(), g_curseGate.load(), g_equipCallbacks.load(), g_equipTicks.load(), g_phasing.load());
}
