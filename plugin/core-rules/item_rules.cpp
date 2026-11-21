// PD2's item rules for Ormus' Robes (docs/research/items/p12-ormus-robes.md):
//
// - properties func 25 (`sorc-skill-rand-ctc`, ProjectDiablo.dll 0x103a12a0) in D2R's free property slot 26: one
//   random skill of PD2's sorceress list gets +param, and the item casts that skill on cast (item_skilloncast) at
//   chance min and level max, both adjusted by the skill as PD2 does.
//   The Cast Triggers companion fires item_skilloncast (IR2, the stat's event in itemstatcost.txt).
// - curses (docs/research/items/ir4-curses.md): max curses (a target holds up to the caster's max_curses + 1 curses) and
//   curse resistance (curse_effectiveness on the target scales the curse's stats), around D2R's ApplyCurse; PD2's
//   gate (#264): resisted-to-0 cancels, stat 109 cuts the duration (cap 75 on a player), attract no longer refuses.
//   Dark Pact (#265) consumes a curse and explodes around the target, scaled by the curses it held.
// - skill while equipped and phasing (docs/research/items/ir7-equipped-items.md): item_skillonequip runs its skill as an
//   item aura (D2R's item_aura case and tick, fed stat 375), and Phasing SelfAura drops the unit collision bits.
// - critical and deadly strike (IR3, docs/research/items/ir3-combat-stats.md): PD2's roll and multipliers in place of
//   D2R's, in WeaponDamage's roll block (a byte patch calling WeaponStrike) and D2R's missile roll.
// - op bases, life steal cap and crushing blow (IR3): equipped_eth / missing_hp kept as base stats from the stat
//   callback, lifedrain_percentcap on D2R's add-life, PD2's blow in CrushingBlowApply's jump slot, and its roll on
//   HitEvents' event 5/6 calls, where PD2's open wounds and deep wounds (event 15) run too.
#include <intrin.h>
#include "item_rules.h"

#define NOMINMAX
#include <windows.h>
#include <D2RLPlugin/lifecycle.h>

#include "map_stack.h"
#include "sig_hook.h"
#include "pd2_engine.h"
#include "rathma_pair.h"
#include "rathma_split.h"
#include "log.h"

#include <D2RLPlugin/mutations.h>
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
constexpr int PropertySlot = 26;           // PD2's func 25; D2R's 25 is affix-rand (tools/pd2_port.py FUNCS)
constexpr int32_t StatSingleSkill = 107;   // item_singleskill
constexpr int32_t StatSkillOnCast = 378;   // item_skilloncast, merged id (PD2 200, merge/ids-itemstatcost.tsv)

std::atomic<uint32_t> g_rolls {0};

// PD2's unlimited books (spell 13, ProjectDiablo.dll): the Horadric Navigator (rtp) opens a town portal and the
// Horadric Almanac (rid) identifies every item at once, and neither is used up. The merge moves them to item spells 17
// and 18 (tools/pd2_port.py FIXES): 17 runs D2R's own Town Portal (entry 2); 18 has no check of its own and its use is
// the Almanac's handler in use slot 14 (almanac.cpp), which identifies all.
constexpr int32_t UnlimitedPortalCheck = 2, UnlimitedPortalUse = 2;

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

// --- Critical and deadly strike (IR3, docs/research/items/ir3-combat-stats.md) ---
// Merged stat ids (merge/ids-itemstatcost.tsv; PD2's 337, 141, 256, 257, 258, 210)
constexpr uint32_t StatPassiveCrit = 337;     // passive_critical_strike
constexpr uint32_t StatDeadlyStrike = 141;    // item_deadlystrike
constexpr uint32_t StatCritMultiplier = 390;  // item_crit_multiplier
constexpr uint32_t StatDsMultiplier = 391;    // item_ds_multiplier
constexpr uint32_t StatDye = 530;            // transform_dye
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
// (docs/research/items/ir3-combat-stats.md)
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

// WeaponDamage's roll block, found by signature (93847: 0x44c2b4 / 0x44c320 / 0x44c3d9). Cast Triggers byte-checks
// 0x44c2a4..0x44c2b8 (its measurement site), so the head is left stock but for the two bytes at +5: a short jump (EB 65)
// to the stub, which sits over the a5 != 0 path's first 28 bytes (the block's `jne` lands there). Both paths now run
// WeaponStrike (#385).
sigscan::Entry CritHead {"WeaponCritHead", "crit", sigscan::Kind::Code, 0, 0,
                         "45 85 f6 75 67 48 8b ce e8 ?? ?? ?? ?? 48 85 c0 74 5a 45 8d 4e 02 45 33 c0 48 8b d0"};
sigscan::Entry CritStub {"WeaponCritStub", "crit", sigscan::Kind::Code, 0, 0,
                         "45 33 c0 ba 51 01 00 00 48 8b ce e8 ?? ?? ?? ?? 8b d8 85 c0 7e 3e 48 8b ce e8"};
sigscan::Entry CritEnd {"WeaponCritEnd", "crit", sigscan::Kind::Code, 0, 0, "8b 6f 20 45 33 c0 48 8b ce 41 8d 50 31"};
sigscan::Entry* const CritOwn[] = {&CritHead, &CritStub, &CritEnd};
constexpr uint8_t CritHeadJoin[] = {0x48, 0x8b};  // mov rcx,rsi's first two bytes, at CritHead + 5
constexpr int CritHeadJoinAt = 5;
constexpr uint8_t CritStubStock[28] = {0x45, 0x33, 0xc0, 0xba, 0x51, 0x01, 0x00, 0x00, 0x48, 0x8b, 0xce, 0xe8, 0xf0, 0x8c,
                                       0xea, 0xff, 0x8b, 0xd8, 0x85, 0xc0, 0x7e, 0x3e, 0x48, 0x8b, 0xce, 0xe8, 0xa2, 0xde};

// The stub: mov rcx,rsi / mov rdx,rdi / mov r8d,r14d / mov rax,WeaponStrike / call rax / jmp CritEnd / nop nop.
// The block's own volatile registers are set again after CritEnd.
auto WeaponCritPatch(uint8_t (&out)[28], uint64_t stub, uint64_t end) noexcept -> void {
	const uint8_t head[] = {0x48, 0x8b, 0xce, 0x48, 0x8b, 0xd7, 0x45, 0x8b, 0xc6, 0x48, 0xb8};
	std::memcpy(out, head, sizeof(head));
	const uint64_t target = reinterpret_cast<uint64_t>(&WeaponStrike);
	std::memcpy(out + 11, &target, 8);
	out[19] = 0xff;
	out[20] = 0xd0;
	out[21] = 0xe9;
	const int32_t rel = static_cast<int32_t>(end - (stub + 26));
	std::memcpy(out + 22, &rel, 4);
	out[26] = 0x90;
	out[27] = 0x90;
}

// The two writes: the jump at CritHead + 5, then the stub. False (nothing written) when a signature is off, the three
// are not the layout the jump encodes (stub = head + 0x6c), or either site's bytes differ.
auto PatchWeaponCrit(const D2RL::PluginContext* context) noexcept -> bool {
	sigscan::Resolve(CritOwn, std::size(CritOwn), context->exeBase, nullptr, 0);
	for (const sigscan::Entry* e : CritOwn) {
		if (e->state != sigscan::State::Ok) {
			return false;
		}
	}
	constexpr uint8_t Jump[] = {0xeb, 0x65};  // 0x44c2b9 + 2 + 0x65 = 0x44c320
	if (CritStub.rva != CritHead.rva + 0x6c || CritEnd.rva != CritHead.rva + 0x125) {
		return false;
	}
	uint8_t stub[28];
	WeaponCritPatch(stub, CritStub.rva, CritEnd.rva);
	// the stub first: alone it is harmless (the a5 != 0 path), and the jump is the only way into it for a5 == 0
	return context->PatchBytes(CritStub.rva, CritStubStock, sizeof(CritStubStock), stub, sizeof(stub)) &&
	       context->PatchBytes(CritHead.rva + CritHeadJoinAt, CritHeadJoin, sizeof(CritHeadJoin), Jump, sizeof(Jump));
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
// itemstatcost.txt gives durability and hitpoints fCallback), missing_hp when life or max life does.
// The server never sends either to the client, so a player's values are mirrored onto its client unit for tooltips.
// PD2's third case, inc_splash_radius + missing_hp x inc_splash_radius_permissinghp (at most 40), is only reached
// with inc_splash_radius as an op base, which no stat has: not ported (docs/research/items/ir3-combat-stats.md).
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

// D2R's unit-stat event function (93847: 0x44d570; game, event, attacker, target, damage), which HitEvents (0x44ce80)
// calls for events 5 and 6 and which reaches D2RCore's dispatcher through the 0x5881e0 thunk and its slot. Taken from
// the two call sites' own rel32 (below), not a fixed address.
using HitEventFn = int32_t (*)(Game*, int32_t, Unit*, Unit*, void*) noexcept;
HitEventFn g_hitEventOriginal = nullptr;

// PD2's open wounds (below): item_openwounds and map_mon_openwounds nodes are hidden the same way
constexpr uint32_t StatOpenWounds = 135;     // item_openwounds
constexpr uint32_t StatMapOpenWounds = 460;  // map_mon_openwounds (PD2 407), the same event
auto OpenWounds(Game* game, Unit* unit, Unit* target, uint32_t stat) noexcept -> void;
thread_local int32_t t_hiddenEvent = -1;  // the event whose nodes an outer HitEvent on this thread has hidden

// Called in place of D2R's unit-stat event function at HitEvents' two calls that raise events 5 and 6 (relayed by
// PatchHitEvents). Not the dispatch slot: Cast Triggers requires D2RCore's own dispatcher there (#385).
auto HitEvent(Game* game, int32_t event, Unit* unit, Unit* target, void* damage) noexcept -> int32_t {
	using namespace d2r::rva;
	if ((event != EventMeleeDamage && event != EventMissileDamage) || game == nullptr || unit == nullptr ||
	    target == nullptr || damage == nullptr) {
		return g_hitEventOriginal(game, event, unit, target, damage);
	}
	uint8_t* hidden[MaxHiddenNodes];
	uint32_t woundStats[MaxHiddenNodes];
	int count = 0, wounds = 0;
	for (auto* node = At<uint8_t*>(unit, UnitEventNodes);
	     node != nullptr && count < MaxHiddenNodes && wounds < MaxHiddenNodes; node = At<uint8_t*>(node, EventNodeNext)) {
		const uint16_t nodeEvent = At<uint16_t>(node, EventNodeEvent);
		const auto stat = static_cast<uint32_t>(At<int32_t>(node, EventNodeStat));
		const bool wound = stat == StatOpenWounds || stat == StatMapOpenWounds;
		// A hit inside the outer dispatch (a splash item event hitting more units) finds the nodes the outer call hid;
		// PD2 runs open wounds on those hits too
		if (nodeEvent == NoEvent && wound && t_hiddenEvent == event) {
			woundStats[wounds++] = stat;
			continue;
		}
		if (nodeEvent != event) {
			continue;
		}
		if (stat == StatCrushingBlow || wound) {
			*reinterpret_cast<uint16_t*>(node + EventNodeEvent) = NoEvent;
			hidden[count++] = node;
			if (stat != StatCrushingBlow) {
				woundStats[wounds++] = stat;
			}
		}
	}
	const int32_t outerHidden = t_hiddenEvent;
	if (count != 0) {
		t_hiddenEvent = event;
	}
	const int32_t result = g_hitEventOriginal(game, event, unit, target, damage);
	t_hiddenEvent = outerHidden;
	for (int i = 0; i < count; ++i) {
		*reinterpret_cast<uint16_t*>(hidden[i] + EventNodeEvent) = static_cast<uint16_t>(event);
	}
	for (int i = 0; i < wounds; ++i) {
		if (d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(target) == 0) {
			OpenWounds(game, unit, target, woundStats[i]);
		}
	}
	const int32_t chance = Stat(unit, StatCrushingBlow) + HandStatSum(unit, StatCrushingBlow);
	if (chance <= 0 || d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(target) != 0) {
		return result;
	}
	void* current = d2r::Engine<void* (*)(Unit*) noexcept>(CurrentSkill)(unit);
	if (current != nullptr &&
	    d2r::Engine<int32_t (*)(const void*, const char*, int32_t) noexcept>(UnitSkillId)(current, __FILE__, __LINE__) ==
	        SkillSmite) {
		return result;
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
	return result;
}

// HitEvents' two calls to the event function, found by their argument set-up (event 6 missile, event 5 melee; 93847:
// 0x44cfc5 and 0x44cfe4), each followed by `E8 rel32`.
sigscan::Entry HitMissileCall {"HitEventMissile", "hitevent", sigscan::Kind::Code, 0, 0,
                               "4c 8b ce 48 89 7c 24 20 4d 8b c6 ba 06 00 00 00 49 8b cf e8 ?? ?? ?? ??"};
sigscan::Entry HitMeleeCall {"HitEventMelee", "hitevent", sigscan::Kind::Code, 0, 0,
                             "4c 8b ce 48 89 7c 24 20 4d 8b c6 ba 05 00 00 00 49 8b cf e8 ?? ?? ?? ??"};
sigscan::Entry* const HitOwn[] = {&HitMissileCall, &HitMeleeCall};
constexpr int HitCallAt = 19;  // the E8 inside the match

// A 14-byte `jmp [rip+0]; &HitEvent` within +-2 GB of the image: a rel32 call reaches it, which the SDK's
// PatchCallRel32 cannot target (plugin memory). No SDK service covers unit events (#385), so the two calls are
// rewritten with the MutationService's byte patch (PatchBytes), as Cast Triggers does for its own relays.
auto AllocateRelay(uint64_t image) noexcept -> uint8_t* {
	// From the far ends of the window, so the near range stays free for other plugins' relays (Cast Triggers'
	// allocation failed intermittently when this took a block next to the image).
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	const uint64_t step = info.dwAllocationGranularity;
	const uint64_t reach = 0x70000000ull;
	const uint64_t low = image > reach ? image - reach : step;
	for (uint64_t at = (low + step - 1) & ~(step - 1); at < image; at += step) {
		if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
			return static_cast<uint8_t*>(p);
		}
	}
	for (uint64_t at = (image + reach) & ~(step - 1); at > image; at -= step) {
		if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
			return static_cast<uint8_t*>(p);
		}
	}
	return nullptr;
}

// Both calls or neither: one MutationService transaction checks and writes both, and restores both on a failure.
auto PatchHitEvents(const D2RL::PluginContext* context) noexcept -> bool {
	sigscan::Resolve(HitOwn, std::size(HitOwn), context->exeBase, nullptr, 0);
	if (HitMissileCall.state != sigscan::State::Ok || HitMeleeCall.state != sigscan::State::Ok) {
		return false;
	}
	const uint64_t sites[2] = {HitMissileCall.address + HitCallAt, HitMeleeCall.address + HitCallAt};
	uint8_t stock[2][5];
	uint64_t target[2];
	for (int i = 0; i < 2; ++i) {
		std::memcpy(stock[i], reinterpret_cast<const void*>(sites[i]), 5);
		int32_t rel;
		std::memcpy(&rel, stock[i] + 1, 4);
		target[i] = sites[i] + 5 + static_cast<int64_t>(rel);
	}
	if (target[0] != target[1]) {
		return false;
	}
	const D2RL::MutationService* mutations = nullptr;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize)) {
		return false;
	}
	uint8_t* relay = AllocateRelay(context->exeBase);
	if (relay == nullptr) {
		return false;
	}
	const uint64_t entry = reinterpret_cast<uint64_t>(&HitEvent);
	const uint8_t jump[6] = {0xff, 0x25, 0, 0, 0, 0};
	std::memcpy(relay, jump, sizeof(jump));
	std::memcpy(relay + sizeof(jump), &entry, 8);
	DWORD old;
	uint8_t call[2][5] = {{0xe8}, {0xe8}};
	bool ok = VirtualProtect(relay, 0x1000, PAGE_EXECUTE_READ, &old) != 0;
	for (int i = 0; ok && i < 2; ++i) {
		const int64_t rel = static_cast<int64_t>(reinterpret_cast<uint64_t>(relay)) - static_cast<int64_t>(sites[i] + 5);
		const int32_t rel32 = static_cast<int32_t>(rel);
		ok = rel == rel32;
		std::memcpy(call[i] + 1, &rel32, 4);
	}
	D2RL::Mutations::TransactionHandle transaction = 0;
	ok = ok && mutations->beginTransaction(context, &transaction) == D2RL::Mutations::Result::Success;
	if (!ok) {
		VirtualFree(relay, 0, MEM_RELEASE);
		return false;
	}
	g_hitEventOriginal = reinterpret_cast<HitEventFn>(target[0]);  // before any call can reach HitEvent
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	for (int i = 0; ok && i < 2; ++i) {
		const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
		                                              .rva = sites[i] - context->exeBase,
		                                              .expected = stock[i],
		                                              .expectedSize = 5,
		                                              .bytes = call[i],
		                                              .size = 5};
		D2RL::Mutations::OperationHandle operation = 0;
		ok = mutations->stageBytePatch(context, transaction, &patch, &operation) == D2RL::Mutations::Result::Success;
	}
	const bool staged = ok;
	if (!ok || mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		// A failed commit that did not restore the stock bytes may leave a call aimed at the relay: keep the page then.
		if (!staged || D2RL::Mutations::HasFlag(result.flags, D2RL::Mutations::CommitFlags::OriginalStateRestored)) {
			VirtualFree(relay, 0, MEM_RELEASE);
		}
		return false;
	}
	return true;
}

// --- Open wounds (IR3): PD2's event 15 (0x103463f0) in place of D2R's ----------------------------------------------
constexpr uint32_t StatLevel = 12;
constexpr uint32_t StatHpRegen = 74;
constexpr uint32_t StatOpenWoundsStack = 373;  // openwounds_stack (PD2 189)
constexpr uint32_t StatDeepWounds = 554;       // deep_wounds (PD2 501)
constexpr uint32_t StatPhysPierce = 478;       // passive_phys_pierce (PD2 425)
constexpr int32_t StateOpenWounds = 62;
constexpr int32_t StateDeepWounds = 271;  // the Barbarian's Deep Wounds passive (PD2 225)
constexpr int32_t BloodGolem = 290;
constexpr int32_t OpenWoundsFrames = 125;  // D2R's 200
constexpr int32_t MaxWoundStacks = 3;
constexpr int32_t PlayerWoundPercent[] = {8, 6, 4};  // on a player, by difficulty (doubles 0x1044fbc0 / b8 / a8)

std::atomic<uint32_t> g_wounds {0};
std::atomic<uint32_t> g_splits {0};
constexpr int32_t StateResistAll = 8;

// PD2's 0x1037c4c0 with 100: value * percent / 100 in doubles, truncated, saturated at INT32_MAX
auto Percent(int32_t value, double percent) noexcept -> int32_t {
	const double v = static_cast<double>(value) * percent / 100.0;
	return v > 2147483647.0 ? 2147483647 : static_cast<int32_t>(v);
}

// 1.13c D2Game 0xac940 with PD2's slopes {9, 18, 27, 36, 45}, plus 40: D2R's own level table (0x584170) is the same
auto WoundsByLevel(int32_t level) noexcept -> int32_t {
	constexpr int32_t s[] = {9, 18, 27, 36, 45};
	int32_t v = 0;
	if (level >= 2) {
		if (level > 60) {
			v = (s[1] + s[2] + s[3]) * 15 + (level - 60) * s[4] + s[0] * 14;
		} else if (level > 45) {
			v = (s[1] + s[2]) * 15 + (level - 45) * s[3] + s[0] * 14;
		} else if (level > 30) {
			v = (level - 30) * s[2] + s[1] * 15 + s[0] * 14;
		} else if (level < 16) {
			v = (level - 1) * s[0];
		} else {
			v = (level - 15) * s[1] + s[0] * 14;
		}
	}
	return v + 40;
}

// PD2's 0x103749f0 (unit, 1) is a player: the monster's owner, or its owner's owner, is a player (a summon, a
// hireling, a summon's own minion)
auto OwnedByPlayer(Game* game, Unit* unit) noexcept -> bool {
	using namespace d2r::rva;
	for (int depth = 0; depth < 2 && unit != nullptr && At<int32_t>(unit, UnitType) == 1; ++depth) {
		const int32_t type = At<int32_t>(unit, UnitOwnerType);
		const uint32_t id = At<uint32_t>(unit, UnitOwnerId);
		if (id == 0 || id == UINT32_MAX) {
			return false;
		}
		if (type == 0) {
			return true;
		}
		unit = type == 1 ? d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(FindUnit)(game, 1, id) : nullptr;
	}
	return false;
}

// PD2's list step (event 15, step 7) on one unit: below 3 stacks the hit adds to the old list's damage and a stack; at 3
// it only refreshes the time. `damage` is the hit's (the list holds it negative); returns the list, `stacks` the count.
auto ApplyWounds(Game* game, Unit* unit, Unit* target, int32_t damage, int32_t* stacks) noexcept -> void* {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto listStat = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue);
	const auto setStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	void* old = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(target, StateOpenWounds);
	*stacks = 1;
	if (old != nullptr) {
		*stacks = listStat(dataFlag, old, StatOpenWoundsStack, 0);
		if (*stacks < MaxWoundStacks) {
			++*stacks;
			damage -= listStat(dataFlag, old, StatHpRegen, 0);  // the older damage, negative
			setStat(dataFlag, old, StatHpRegen, -damage, 0);
		}
	}
	d2r::StateListRequest request {unit, target, 0, 1, OpenWoundsFrames, static_cast<int32_t>(StatHpRegen), -damage,
	                               StateOpenWounds, nullptr};
	void* list = d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	if (list != nullptr) {
		setStat(dataFlag, list, StatOpenWoundsStack, *stacks, 0);
	}
	return list;
}

// PD2's open wounds on a hit, one roll per event node of the stat (PD2's dispatcher runs each node):
// - chance: the node's stat on the attacker, % 100 on its seed;
// - damage per frame (1/256 life): the level table + 5 x deep_wounds. On a player, the level part times 8 / 6 / 4%
//   by difficulty; deep_wounds the same (half again from a Blood Golem), the Deep Wounds passive's part a third of
//   that;
// - less the target's damageresist after the attacker's passive_phys_pierce (a negative result counts half; 100 or
//   more ignores the pierce): a player loses at most 50%. A monster a player owns takes a quarter;
// - on one of Rathma's clones with both boss slots set, the damage is halved and the other clone,
//   unless it is dying or dead or has resistall (state 8), gets the same half on its own list (#645; the lookup is common/rathma_pair.h,
//   shared with core-events' rathma_life.cpp);
// - hpregen on the target's open wounds list (125 frames). Below 3 stacks the hit adds to the list's damage and a
//   stack; at 3 it only refreshes the time (NewStateList keeps an older list of the same skill and level).
// shortcut: PD2 keys the list by attacker (0x102fc880), so two attackers stack apart; D2R keeps one open wounds list
// per unit, which all attackers share. Not ported: no damage to a player inside PD2's PvP maps, cut (not in
// the merged data) and PD2's damage meter.
auto OpenWounds(Game* game, Unit* unit, Unit* target, uint32_t stat) noexcept -> void {
	using namespace d2r::rva;
	const int32_t chance = Stat(unit, stat);
	if (chance <= 0) {
		return;
	}
	const int32_t roll = static_cast<int32_t>(d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit), 100));
	if (roll >= chance) {
		return;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto listStat = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue);
	const auto stateList = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList);
	const bool player = At<int32_t>(target, UnitType) == 0;
	int32_t base = WoundsByLevel(std::max(Stat(unit, StatLevel), 1));
	int32_t deep = Stat(unit, StatDeepWounds);
	if (player) {
		const uint8_t difficulty = std::min<uint8_t>(At<uint8_t>(game, GameDifficulty), 2);
		const double percent = PlayerWoundPercent[difficulty];
		base = Percent(base, percent);
		const double deepPercent =
			At<int32_t>(unit, UnitType) == 1 && At<int32_t>(unit, UnitClassId) == BloodGolem ? percent * 0.5 : percent;
		void* passive = stateList(unit, StateDeepWounds);
		const int32_t fromPassive = passive != nullptr ? listStat(dataFlag, passive, StatDeepWounds, 0) : 0;
		deep = passive != nullptr ? Percent(deep - fromPassive, deepPercent) + Percent(fromPassive, deepPercent / 3.0)
		                          : Percent(deep, deepPercent);
	}
	int32_t damage = base + deep * 5;
	const int32_t resist = Stat(target, StatDamageResist);
	int32_t r = resist < 100 ? resist - Stat(unit, StatPhysPierce) : 100;
	if (r < 0) {
		r /= 2;
	}
	damage = player && r > 50 ? Percent(damage, 50.0) : Percent(damage, 100.0 - r);
	if (!player && OwnedByPlayer(game, target)) {
		damage /= 4;
	}
	Unit* other = pd2::rathma::Other(game, target);
	if (other != nullptr) {
		damage = pd2::rathma::Half(damage);
	}
	int32_t stacks = 0;
	void* list = ApplyWounds(game, unit, target, damage, &stacks);
	if (other != nullptr) {
		const int32_t mode = At<int32_t>(other, UnitMode);
		const bool otherList = mode != 0 && mode != 12 &&
			d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(other, StateResistAll) == 0;
		int32_t otherStacks = 0;
		if (otherList) {
			ApplyWounds(game, unit, other, damage, &otherStacks);
		}
		if (const uint32_t m = ++g_splits; m <= 20 || m % 100 == 0) {
			plog::Info("open wounds split: class %d takes %d/256 per frame (halved), class %d list %s, stack %d (%u so far)",
			           At<int32_t>(target, UnitClassId), damage, At<int32_t>(other, UnitClassId), otherList ? "yes" : "no",
			           otherStacks, m);
		}
	}
	const uint32_t n = ++g_wounds;
	if (At<int32_t>(unit, UnitType) == 0 || n <= 20) {
		plog::Info("open wounds: unit type %d on unit type %d class %d, chance %d roll %d, level part %d, deep %d, "
		           "resist %d, %d/256 per frame, stack %d%s, %u so far", At<int32_t>(unit, UnitType),
		           At<int32_t>(target, UnitType), At<int32_t>(target, UnitClassId), chance, roll, base, deep, r,
		           list != nullptr ? -listStat(dataFlag, list, StatHpRegen, 0) : damage, stacks,
		           list != nullptr ? "" : " (refused)", n);
	}
}

// --- Curses: curse resistance and max curses (docs/research/items/ir4-curses.md) ---------------------------------------
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

// --- Dark Pact (#265, docs/research/items/ir4-curses.md): PD2's state list creator 0x1036d7d0, state 196 ----------------
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

// --- Skill while equipped and phasing (docs/research/items/ir7-equipped-items.md) -------------------------------------
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

// --- Replenish charges (IR5) ---------------------------------------------------------------------------------------
// PD2 (ProjectDiablo.dll 0x10345130, 0x103450b0, 0x103451a0) runs item_replenish_charges on the replenish event (3):
// equipping an item with the stat schedules it 2500 / stat + 1 frames on, and each run adds one charge to the item's
// first charged skill below its max (sent to the owner's client) and schedules the next. No SDK service covers unit
// events or charged skills, so both D2R functions are hooked: the scheduler, which every equip calls, and the handler.
// PD2's handler (0x10345320) runs the durability step, else the quantity step, and only when neither added anything
// the charge step, which schedules the next run whatever is pending (D2R frees the firing event only after its handler
// returns, so a pending check there always sees it). The port matches: D2R's step first, then the charges only when
// durability and quantity are unchanged. No merged item has both stats.
// cut: PD2 halves the delay in PD2's PvP maps, cut.
constexpr uint32_t StatReplenishCharges = 475;  // item_replenish_charges (PD2's 422)
constexpr uint32_t StatChargedSkill = 204;      // item_charged_skill: charges | max << 8
constexpr int32_t ReplenishFrames = 2500;

using ScheduleReplenishFn = void (*)(Game* game, Unit* item) noexcept;
ScheduleReplenishFn g_scheduleReplenishOriginal = nullptr;
using ReplenishEventFn = void (*)(Game* game, Unit* item, int32_t type) noexcept;  // 0x4730d0 passes these three
ReplenishEventFn g_replenishEventOriginal = nullptr;
std::atomic<uint32_t> g_chargesReplenished {0};

// The next run, 2500 / stat + 1 frames on; `pending` first looks for a replenish event already queued (the scheduler's
// check; PD2's 0x103450b0)
void ScheduleCharges(Game* game, Unit* item, bool pending) noexcept {
	using namespace d2r::rva;
	const int32_t rate = Stat(item, StatReplenishCharges);
	if (rate <= 0 || (pending && d2r::Engine<int32_t (*)(Game*, Unit*, int32_t, int32_t) noexcept>(UnitEventFrame)(
	                                 game, item, UnitEventReplenish, 0) != 0)) {
		return;
	}
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(AddUnitEvent)(
		game, item, UnitEventReplenish, At<int32_t>(game, GameFrame) + 1 + ReplenishFrames / rate, 0, 0, 0);
}

auto ReplenishSchedule(Game* game, Unit* item) noexcept -> void {
	g_scheduleReplenishOriginal(game, item);
	if (game != nullptr && item != nullptr) {
		ScheduleCharges(game, item, true);
	}
}

// One charge back on the item's first charged skill, as the charge use (0x4368a0) moves it: the list that holds it, the
// value set there, and the stat sent to the owner's client.
// shortcut: PD2 takes the first item_charged_skill of the item's own list (flag 0x40); this takes the first of its full
// stats, the same entry unless a socketed item adds a charged skill. Upgrade if one does.
void AddCharge(Game* game, Unit* item) noexcept {
	using namespace d2r::rva;
	using Entry = pd2::mapstack::Entry;  // 16 bytes under D2RCore (map_stack.h)
	Entry entries[64] {};
	const int32_t count = d2r::Engine<int32_t (*)(Unit*, uint32_t, Entry*, int32_t) noexcept>(UnitStatEntries)(
		item, StatChargedSkill, entries, 64);
	const uint32_t dataFlag = d2r::DataFlag(game);
	if (count <= 0 || DataTables(dataFlag) == nullptr) {
		return;
	}
	const uint32_t layer = static_cast<uint32_t>(entries[0].layer);
	const int32_t skill = static_cast<int32_t>(layer >> LayerShift(dataFlag));
	const int32_t level = static_cast<int32_t>(layer & LayerMask(dataFlag));
	int32_t charges = 0;
	void* list = nullptr;
	if (d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t, int32_t*, void**) noexcept>(ChargedSkillList)(
	        item, skill, level, &charges, &list) == 0 || list == nullptr) {
		return;
	}
	const int32_t value = d2r::Engine<int32_t (*)(uint32_t, void*, uint32_t, uint32_t) noexcept>(ListStatValue)(
		dataFlag, list, StatChargedSkill, layer);
	if ((value & 0xff) >= (value >> 8)) {
		return;
	}
	d2r::Engine<void (*)(uint32_t, void*, uint32_t, int32_t, uint32_t, Unit*) noexcept>(SetListStatOf)(
		dataFlag, list, StatChargedSkill, value + 1, layer, item);
	const uint32_t n = ++g_chargesReplenished;
	if (n <= 20 || n % 100 == 0) {
		plog::Info("replenish charges: skill %d level %d, %d -> %d of %d, %u so far", skill, level, value & 0xff,
		           (value & 0xff) + 1, value >> 8, n);
	}
	const int32_t ownerId = d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemOwnerId)(item);
	Unit* owner = ownerId != -1 ? d2r::Engine<Unit* (*)(Game*, int32_t, int32_t) noexcept>(FindUnit)(game, 0, ownerId)
	                            : nullptr;
	// The playerbody guard is D2R's durability step's (0x46f4f0); PD2's charge step sends to any player owner
	if (owner == nullptr || d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(owner, StatePlayerBody) != 0) {
		return;
	}
	if (void* client = d2r::Engine<void* (*)(Unit*) noexcept>(PlayerClient)(owner)) {
		d2r::Engine<void (*)(void*, Unit*, int32_t, uint32_t, int32_t, uint32_t) noexcept>(SendUnitStat)(
			client, item, 1, StatChargedSkill, value + 1, layer);
	}
}

auto ChargesReplenishEvent(Game* game, Unit* item, int32_t type) noexcept -> void {
	using namespace d2r::rva;
	const int32_t durability = item != nullptr ? Stat(item, StatDurability) : 0;
	const int32_t quantity = item != nullptr ? Stat(item, StatQuantity) : 0;
	g_replenishEventOriginal(game, item, type);
	if (game == nullptr || item == nullptr || Stat(item, StatReplenishCharges) <= 0 ||
	    Stat(item, StatDurability) != durability || Stat(item, StatQuantity) != quantity) {
		return;
	}
	AddCharge(game, item);
	ScheduleCharges(game, item, false);
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

// The unlimited books' item spell entries (17 and 18), written through the SDK's MutationService (no service adds an
// item spell); the patch is refused unless both entries are still empty.
static auto PatchUnlimitedBooks(const D2RL::PluginContext* context) noexcept -> bool {
	using namespace d2r::rva;
	const D2RL::MutationService* mutations = nullptr;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize) ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		return false;
	}
	static constexpr uint8_t Empty[2 * ItemSpellEntrySize] = {};
	static constexpr int32_t Books[2 * 3] = {UnlimitedPortalCheck, UnlimitedPortalUse, 1, 0, AlmanacUseSlot, 1};
	static_assert(sizeof(Books) == sizeof(Empty));
	const D2RL::Mutations::BytePatchRequest patch{
		.structSize = D2RL::Mutations::BytePatchRequestSize,
		.rva = ItemSpellTable + static_cast<uint64_t>(UnlimitedPortalSpell) * ItemSpellEntrySize,
		.expected = Empty,
		.expectedSize = sizeof(Empty),
		.bytes = Books,
		.size = sizeof(Books)};
	D2RL::Mutations::OperationHandle operation = 0;
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->stageBytePatch(context, transaction, &patch, &operation) != D2RL::Mutations::Result::Success ||
	    mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		return false;
	}
	return true;
}

// --- Charm rows (#358) ---------------------------------------------------------------------------------------------
// PD2's inventory is 10x8 and a charm works only in its lower four rows (the grid's y 4 to 7, by the charm's top row).
// D2R's own charm-active body (93847: 0x36ae00) is not called: D2RLoader rewrote its callers to a thunk in its .maho
// section whose `jmp [slot]` reaches D2RCore's charm policy (stock inventory, custom page or both). No SDK service
// covers a cell range in the stock inventory, so the slot is swapped for a wrapper (as for crushing blow), found
// from D2Game's add-to-inventory call site, never by a fixed address.
sigscan::Entry CharmCallSite {"ItemCharmActiveCall", "charms", sigscan::Kind::Code, 0, 0,
                              "48 8b cb e8 ?? ?? ?? ?? 83 f8 01 74 ?? 48 8b cb e8 ?? ?? ?? ?? 85 c0 75 ?? 48 8b d7 48 8b cb e8 ?? ?? ?? ?? 85 c0 74 ?? 0f b6 8e 06 01 00 00 41 b9 01 00 00 00"};
constexpr uint32_t CharmCallOffset = 31;  // the E8 of the charm check in that signature
constexpr uint32_t CharmFirstActiveRow = 4;  // zero-based: rows 5 to 8

using CharmActiveFn = int32_t (*)(Unit* item, Unit* player) noexcept;
CharmActiveFn g_charmActiveOriginal = nullptr;
std::atomic<uint32_t> g_charmChecks {0};
std::atomic<uint32_t> g_charmOffChecks {0};

// A unique charm of the same name in the active rows too (#1288): D2R's (item) -> unique row + 1, the cube executor's read.
constexpr uint64_t ItemUniqueRow = 0x36ce70;
constexpr int32_t QualityUnique = 7;

// Stats follow the policy only when the engine asks it: link (0x475140 -> StatListLink 0x2f81a0) happens on a yes from the add
// path 0x475090, identify 0x46e8c0 / 0x46ea70 and the refresh 0x470ca0, which asks about every stored grid item, linked or not,
// but only links (never unlinks). An item leaving the grid is unlinked by 0x46f710 inside the move (0x4747f0), then the move
// runs the refresh (0x471e90 / 0x472590). So: on any ask about a unique charm with a twin, answer no and unlink every linked
// copy (the item too) with StatListUnlink 0x2f8290; when a copy leaves, the refresh asks about the other one, finds no twin and
// links it. No return address is needed: the rule is the same whoever asks.
constexpr uint64_t ItemStatsAttached = 0x2f8120;  // (item, int* out) -> the item's stat list's owner, 0 when not linked
constexpr uint64_t StatListUnlink = 0x2f8290;     // (owner, item), toolkit stats.h
constexpr uint32_t CharmTwinLogCap = 200;
std::atomic<uint32_t> g_charmTwinLogs {0};

// Unlinks item if its stats are on; true when it did.
auto UnlinkIfAttached(Unit* item, Unit* player) noexcept -> bool {
	if (d2r::Engine<uint64_t (*)(Unit*, int32_t*) noexcept>(ItemStatsAttached)(item, nullptr) == 0) {
		return false;
	}
	d2r::Engine<void (*)(Unit*, Unit*) noexcept>(StatListUnlink)(player, item);
	return true;
}

// Same-name unique charms stored (mode 0, so never the cursor) with their top row in rows 5-8, item excluded; each linked one is
// unlinked (counted in unlinked; pass nullptr to only count, as the draw path must). Returns how many there are, 0 when item is not in the player's item list: the client's remove
// handler 0x1c60a0 asks about the leaving copy after 0x389820 took it out (mode and cell unchanged) and unlinks it and refreshes
// 0x1c9610 only on a yes, so a leaving copy gets D2RCore's answer. A twin D2RCore refuses (unidentified, requirements) still counts.
auto CountTwins(Unit* item, Unit* player, uint32_t* unlinked) noexcept -> uint32_t {
	using namespace d2r::rva;
	if (player == nullptr || d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality)(item) != QualityUnique) {
		return 0;
	}
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory)(player, __FILE__, __LINE__);
	if (inventory == nullptr) {
		return 0;
	}
	const auto uniqueRow = d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemUniqueRow);
	const auto next = d2r::Engine<Unit* (*)(Unit*) noexcept>(InventoryNextItem);
	const int32_t mine = uniqueRow(item);
	Unit* const first = d2r::Engine<Unit* (*)(void*) noexcept>(InventoryFirstItem)(inventory);
	Unit* listed = first;
	while (listed != nullptr && listed != item) {
		listed = next(listed);
	}
	if (listed == nullptr) {
		return 0;
	}
	uint32_t found = 0;
	for (Unit* other = first; other != nullptr; other = next(other)) {
		if (other != item && At<int32_t>(other, UnitType) == 4 &&
		    d2r::Engine<uint32_t (*)(Unit*) noexcept>(UnitModeOf)(other) == 0 &&
		    d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemPage)(other) == 0 &&
		    (d2r::Engine<uint32_t (*)(Unit*) noexcept>(UnitPackedXY)(other) >> 16) >= CharmFirstActiveRow &&
		    d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality)(other) == QualityUnique && uniqueRow(other) == mine) {
			++found;
			if (unlinked != nullptr) {
				*unlinked += UnlinkIfAttached(other, player) ? 1 : 0;
			}
		}
	}
	return found;
}

// True when D2R counts the charm as active but it sits in a top grid row: PD2 switches it off.
auto CharmSwitchedOff(Unit* item, Unit* player, uint32_t& xy, uint8_t& page) noexcept -> bool {
	if (item == nullptr || g_charmActiveOriginal(item, player) == 0) {
		return false;
	}
	page = d2r::Engine<uint8_t (*)(Unit*) noexcept>(d2r::rva::ItemPage)(item);
	xy = d2r::Engine<uint32_t (*)(Unit*) noexcept>(d2r::rva::UnitPackedXY)(item);
	return page == 0 && (xy >> 16) < CharmFirstActiveRow;  // a custom page is never judged by its grid row
}

// True for a same-name unique charm stored in rows 5-8 with a twin there (#1288): both go inactive. Only counts unless
// `unlinked` is given, so the draw path (#1377) shares it without changing state.
auto CharmTwinOff(Unit* item, Unit* player, uint32_t xy, uint8_t page, uint32_t* unlinked) noexcept -> bool {
	return page == 0 && (xy >> 16) >= CharmFirstActiveRow &&
	       d2r::Engine<uint32_t (*)(Unit*) noexcept>(d2r::rva::UnitModeOf)(item) == 0 && CountTwins(item, player, unlinked) != 0;
}

auto CharmActiveInRow(Unit* item, Unit* player) noexcept -> int32_t {
	const int32_t active = g_charmActiveOriginal(item, player);
	if (active == 0 || item == nullptr) {
		return active;
	}
	uint32_t xy = 0;
	uint8_t page = 0;
	// a same-name unique in the active rows switches both off (#1288); a cursor-held copy (mode 4) never counts
	uint32_t unlinked = 0;
	bool off = CharmSwitchedOff(item, player, xy, page);
	const uint32_t row = xy >> 16;
	const bool twin = !off && CharmTwinOff(item, player, xy, page, &unlinked);
	if (twin) {
		unlinked += UnlinkIfAttached(item, player) ? 1 : 0;
		off = true;
	}
	const uint32_t n = off ? ++g_charmOffChecks : ++g_charmChecks;  // own cap, so a top-row charm cannot hide kept lines or flood
	if (n <= 40 || n % 500 == 0 || (twin && ++g_charmTwinLogs <= CharmTwinLogCap)) {  // a twin answer is the #1288 case
		plog::Info("charm rows: item %p at x %u y %u, page %u, quality %d, unique row+1 %d, same-name twin in rows 5-8 %s, %u stat lists unlinked, active in D2R, %s, %u so far",
		           static_cast<void*>(item), xy & 0xffff, row, page,
		           d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::ItemQuality)(item),
		           d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemUniqueRow)(item), twin ? "yes" : "no", unlinked, off ? "switched off" : "kept", n);
	}
	return off ? 0 : active;
}

// The inventory widget's draw (93847: 0x2c3570) asks D2RCore's "requirements met for display" check for each stored
// item (a .maho thunk, its slot swapped like the charm policy's) and draws the item's cell in D2R's red unusable tint
// when it says no. A top-row charm gives no stats, so PD2 tints it red like any inactive item (#1358): the check says
// no for it. The draw is that call's only caller, so no rule sees the answer. The arguments are the call site's
// (item, player, four zero words, the last a dword on the stack), passed on as given.
sigscan::Entry ReqDisplaySite {"ItemReqDisplayCall", "charms", sigscan::Kind::Code, 0, 0,
                               "33 c0 45 33 c9 89 44 24 30 45 33 c0 48 89 44 24 28 49 8b d4 48 8b cf 48 89 44 24 20 e8 ?? ?? ?? ?? 85 c0 0f 84 ?? ?? ?? ?? 84 db 0f 85"};
constexpr uint32_t ReqDisplayCallOffset = 28;  // the E8 of the check in that signature

using ReqDisplayFn = int32_t (*)(Unit* item, Unit* player, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) noexcept;
ReqDisplayFn g_reqDisplayOriginal = nullptr;

auto ReqDisplayInRow(Unit* item, Unit* player, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) noexcept -> int32_t {
	const int32_t ok = g_reqDisplayOriginal(item, player, a3, a4, a5, a6, a7);
	uint32_t xy = 0;
	uint8_t page = 0;
	// a same-name twin in rows 5-8 is switched off too (#1377); counted only, the draw never unlinks
	return ok != 0 && (CharmSwitchedOff(item, player, xy, page) || (g_charmActiveOriginal(item, player) != 0 && CharmTwinOff(item, player, xy, page, nullptr))) ? 0 : ok;
}

auto ThunkSlotRva(const sigscan::Entry& entry, uint32_t callOffset) noexcept -> uint64_t {
	if (entry.state != sigscan::State::Ok) {
		return 0;
	}
	const auto* const call = reinterpret_cast<const uint8_t*>(d2r::imageBase + entry.rva + callOffset);
	if (call[0] != 0xe8) {
		return 0;
	}
	const uint8_t* const thunk = call + 5 + *reinterpret_cast<const int32_t*>(call + 1);
	if (thunk[0] != 0xff || thunk[1] != 0x25) {  // jmp [rip+disp32]; anything else is not D2RLoader's thunk
		return 0;
	}
	const uint8_t* const slot = thunk + 6 + *reinterpret_cast<const int32_t*>(thunk + 2);
	return reinterpret_cast<uint64_t>(slot) - d2r::imageBase;
}

auto SwapThunkSlot(const D2RL::PluginContext* context, const sigscan::Entry& entry, uint32_t callOffset, uint64_t wrapper,
                   uint64_t* original) noexcept -> bool {
	const uint64_t slotRva = ThunkSlotRva(entry, callOffset);
	if (slotRva == 0) {
		return false;
	}
	const uint64_t old = *reinterpret_cast<const uint64_t*>(d2r::imageBase + slotRva);
	if (old == 0) {
		return false;
	}
	*original = old;
	if (!context->PatchWriteU64(slotRva, &old, sizeof(old), wrapper)) {
		return false;
	}
	plog::Info("%s: %s slot at image +0x%llx swapped", entry.area, entry.name, static_cast<unsigned long long>(slotRva));
	return true;
}

// PD2's dye (transform_dye, set by the corruption recipes) recolours the item sprite, never its cell (#1529). PD2 stores the
// colors.txt row + 1 (0 = none; the Annihilus corruptions' 10 is cred). No PluginSDK service draws items, so two draw paths
// are hooked, each by a unique signature:
// - Legacy: D2R picks an item's colour in one function (93847: 0x3710d0, D2Common Items.cpp): (player, item, out packed
//   index, 0 body / 1 inventory) -> the 256-byte palette shift of (items.txt Transform +0x151 or InvTrans +0x152, colors.txt
//   row), or null; its unique and set branches read invtransform the same way. A dyed item takes that path with the dye.
// - HD: the shift above becomes a colour transform class (itemtransforms.json) that recolours only the hues it targets, so
//   most HD art keeps its colour. The HD item draw (0xc7020: item, sprite, packed xy, scale, alpha; inventory and cursor)
//   fills a draw record and passes it to the UI sprite draw (0x858510: sprite, xy, record, scale). For a dyed item the
//   record drops the transform (+0x1d) and takes the dye as its RGBA multiplier (+0x00, weight +0x0c = 1), as in game.
//   Only that one call (0xc71f3) is redirected, not 0x858510's entry, which other plugins hook (better-automap). Both
//   callers of 0xc7020 run it only in HD (they test 0x846210 first and draw legacy items elsewhere), so no mode test here.
using ItemColorFn = const uint8_t* (*)(Unit* player, Unit* item, uint8_t* index, int32_t inventory) noexcept;
using PaletteShiftFn = const uint8_t* (*)(int32_t type, int32_t color) noexcept;
using ItemDrawFn = void (*)(Unit* item, void* sprite, uint64_t xy, float scale, void* alpha) noexcept;
using SpriteDrawFn = void (*)(void* sprite, int32_t* xy, float* record, float scale) noexcept;
ItemColorFn g_itemColorOriginal = nullptr;
PaletteShiftFn g_paletteShift = nullptr;
ItemDrawFn g_itemDrawOriginal = nullptr;
SpriteDrawFn g_spriteDrawOriginal = nullptr;
thread_local int32_t g_drawDye = -1;  // the dye of the item whose HD draw is running, -1 none
std::atomic<uint32_t> g_dyeColors {0};
std::atomic<uint32_t> g_dyeDraws {0};

// RGB multiplier per colors.txt row, in its order (the test checks the codes).
constexpr float DyeRgb[21][3] = {
    {1.0f, 1.0f, 1.0f},     // whit
    {0.8f, 0.8f, 0.8f},     // lgry
    {0.5f, 0.5f, 0.5f},     // dgry
    {0.25f, 0.25f, 0.25f},  // blac
    {0.5f, 0.7f, 1.0f},     // lblu
    {0.3f, 0.4f, 0.8f},     // dblu
    {0.3f, 0.6f, 1.0f},     // cblu
    {1.0f, 0.5f, 0.5f},     // lred
    {0.6f, 0.15f, 0.15f},   // dred
    {1.0f, 0.2f, 0.2f},     // cred
    {0.5f, 1.0f, 0.5f},     // lgrn
    {0.2f, 0.55f, 0.2f},    // dgrn
    {0.3f, 1.0f, 0.4f},     // cgrn
    {1.0f, 1.0f, 0.55f},    // lyel
    {0.75f, 0.7f, 0.2f},    // dyel
    {1.0f, 0.85f, 0.45f},   // lgld
    {0.75f, 0.6f, 0.25f},   // dgld
    {0.8f, 0.5f, 1.0f},     // lpur
    {0.5f, 0.25f, 0.7f},    // dpur
    {1.0f, 0.6f, 0.2f},     // oran
    {1.0f, 1.0f, 1.0f},     // bwht
};

auto DyeOf(Unit* item) noexcept -> int32_t {
	const int32_t color = item != nullptr ? Stat(item, StatDye) - 1 : -1;
	return color >= 0 && color < 21 ? color : -1;
}

auto DyedItemColor(Unit* player, Unit* item, uint8_t* index, int32_t inventory) noexcept -> const uint8_t* {
	const int32_t color = DyeOf(item);
	const auto* row = color >= 0 ? d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::rva::ItemRecord)(
	                                   d2r::api.unitDataFlag(item), At<int32_t>(item, d2r::rva::UnitClassId))
	                             : nullptr;
	if (row == nullptr) {
		return g_itemColorOriginal(player, item, index, inventory);
	}
	// The unique branch packs the body type, which charms lack (Transform 0), so a type-0 item packs its InvTrans.
	const uint8_t type = inventory != 0 ? row[0x152] : row[0x151];
	const uint8_t packed = row[0x151] != 0 ? row[0x151] : type;
	*index = packed >= 1 && packed <= 8 ? static_cast<uint8_t>(packed << 5 | color) : 0;
	if (plog::Sample(g_dyeColors)) {
		plog::Info("dye tint: item %p colour %d, %s, type %u (%u so far)", static_cast<void*>(item), color,
		           inventory != 0 ? "inventory" : "body", type, g_dyeColors.load());
	}
	return g_paletteShift(type, color);
}

auto DyedItemDraw(Unit* item, void* sprite, uint64_t xy, float scale, void* alpha) noexcept -> void {
	g_drawDye = DyeOf(item);
	g_itemDrawOriginal(item, sprite, xy, scale, alpha);
	g_drawDye = -1;
}

auto DyedSpriteDraw(void* sprite, int32_t* xy, float* record, float scale) noexcept -> void {
	const int32_t color = g_drawDye;
	if (color < 0 || record == nullptr) {
		return g_spriteDrawOriginal(sprite, xy, record, scale);
	}
	g_drawDye = -1;  // the item draw's one sprite: nothing it draws later is tinted
	alignas(16) float copy[0x2a];  // the record (0xa8 bytes, the item draw's local); D2R's stays as it was
	std::memcpy(copy, record, sizeof(copy));
	std::memcpy(copy, DyeRgb[color], sizeof(DyeRgb[color]));
	copy[3] = 1.0f;
	reinterpret_cast<uint8_t*>(copy)[0x1d] = 0;
	if (plog::Sample(g_dyeDraws)) {
		plog::Info("dye tint: HD sprite colour %d, rgb %.2f %.2f %.2f (%u so far)", color, copy[0], copy[1], copy[2],
		           g_dyeDraws.load());
	}
	g_spriteDrawOriginal(sprite, xy, copy, scale);
}

// The HD item draw's one call of the UI sprite draw (93847: 0xc71f3, `E8 rel32` at +22 of this match) is pointed at a
// relay page that jumps to DyedSpriteDraw, by a MutationService byte patch with the stock call as expected bytes.
auto PatchItemSpriteCall(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t match = 0;
	if (sigscan::ScanUnique(context->exeBase,
	                        "f3 0f 11 44 24 70 0f 28 de 4c 8d 44 24 60 48 8d 54 24 28 49 8b cf e8 ?? ?? ?? ?? c6 47 04 00", 0,
	                        &match) != 1) {
		return false;
	}
	const uint64_t site = context->exeBase + match + 22;
	uint8_t stock[5];
	std::memcpy(stock, reinterpret_cast<const void*>(site), 5);
	int32_t rel = 0;
	std::memcpy(&rel, stock + 1, 4);
	const D2RL::MutationService* mutations = nullptr;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize)) {
		return false;
	}
	uint8_t* relay = AllocateRelay(context->exeBase);
	if (relay == nullptr) {
		return false;
	}
	const uint64_t entry = reinterpret_cast<uint64_t>(&DyedSpriteDraw);
	const uint8_t jump[6] = {0xff, 0x25, 0, 0, 0, 0};  // jmp [rip+0]; &DyedSpriteDraw
	std::memcpy(relay, jump, sizeof(jump));
	std::memcpy(relay + sizeof(jump), &entry, 8);
	const int64_t reach = static_cast<int64_t>(reinterpret_cast<uint64_t>(relay)) - static_cast<int64_t>(site + 5);
	const int32_t rel32 = static_cast<int32_t>(reach);
	uint8_t call[5] = {0xe8};
	std::memcpy(call + 1, &rel32, 4);
	DWORD old;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (reach != rel32 || VirtualProtect(relay, 0x1000, PAGE_EXECUTE_READ, &old) == 0 ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		VirtualFree(relay, 0, MEM_RELEASE);
		return false;
	}
	g_spriteDrawOriginal = reinterpret_cast<SpriteDrawFn>(site + 5 + static_cast<int64_t>(rel));  // before the call can land
	const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
	                                              .rva = site - context->exeBase,
	                                              .expected = stock,
	                                              .expectedSize = 5,
	                                              .bytes = call,
	                                              .size = 5};
	D2RL::Mutations::OperationHandle operation = 0;
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	const bool staged = mutations->stageBytePatch(context, transaction, &patch, &operation) == D2RL::Mutations::Result::Success;
	if (!staged || mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		// a failed commit that did not restore the stock call may still aim at the relay: keep the page then
		if (!staged || D2RL::Mutations::HasFlag(result.flags, D2RL::Mutations::CommitFlags::OriginalStateRestored)) {
			VirtualFree(relay, 0, MEM_RELEASE);
		}
		return false;
	}
	return true;
}

// "" when both parts are in, else which part failed (for the install report).
auto InstallDyeTint(const D2RL::PluginContext* context) noexcept -> const char* {
	uint64_t shift = 0;
	static constexpr uint8_t ColorEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x44, 0x89, 0x4c, 0x24, 0x20, 0x55, 0x56, 0x57,
	                                         0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
	// mov r11,rsp / push rbp / push rdi / push r15 / lea rbp,[r11-0xb8]
	static constexpr uint8_t DrawEntry[] = {0x4c, 0x8b, 0xdc, 0x55, 0x57, 0x41, 0x57, 0x49, 0x8d, 0xab, 0x48, 0xff, 0xff, 0xff};
	if (sigscan::ScanUnique(context->exeBase, "48 89 5c 24 10 57 48 83 ec 20 48 63 d9 48 63 fa 83 fb 09 7d 05 83 ff 15 7c 1e", 0, &shift) == 1) {
		g_paletteShift = reinterpret_cast<PaletteShiftFn>(context->exeBase + shift);
	}
	const bool legacy = g_paletteShift != nullptr &&
	                    sigscan::HookBySignature(
	                        context, "ItemColor",
	                        "48 89 5c 24 08 44 89 4c 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8b ec 48 83 ec 30 45 8b e1 4d 8b e8 48 8b fa 4c 8b f9 48 85 d2 0f 84",
	                        0, ColorEntry, sizeof(ColorEntry), DyedItemColor, &g_itemColorOriginal);
	// the call first: the item draw hook only marks the draw the redirected call tints
	const bool hd = PatchItemSpriteCall(context) &&
	                sigscan::HookBySignature(context, "HdItemDraw",
	                                         "4c 8b dc 55 57 41 57 49 8d ab 48 ff ff ff 48 81 ec a0 01 00 00 41 0f 29 73 b8", 0,
	                                         DrawEntry, sizeof(DrawEntry), DyedItemDraw, &g_itemDrawOriginal);
	return legacy && hd ? ""
	       : hd         ? " FAILED (legacy colour: signature or bytes differ)"
	       : legacy     ? " FAILED (HD sprite: signature, bytes or relay)"
	                    : " FAILED (legacy colour and HD sprite)";
}

auto InstallCharmRows(const D2RL::PluginContext* context) noexcept -> bool {
	sigscan::Entry* const own[] = {&CharmCallSite, &ReqDisplaySite};
	sigscan::Resolve(own, 2, context->exeBase, nullptr, 0);
	if (!SwapThunkSlot(context, CharmCallSite, CharmCallOffset,
	                   reinterpret_cast<uint64_t>(static_cast<CharmActiveFn>(CharmActiveInRow)),
	                   reinterpret_cast<uint64_t*>(&g_charmActiveOriginal))) {
		return false;
	}
	// The tint is cosmetic and the rows work without it, so a miss is only logged.
	if (!SwapThunkSlot(context, ReqDisplaySite, ReqDisplayCallOffset,
	                   reinterpret_cast<uint64_t>(static_cast<ReqDisplayFn>(ReqDisplayInRow)),
	                   reinterpret_cast<uint64_t*>(&g_reqDisplayOriginal))) {
		plog::Warn("charm rows: the red tint hook was not installed (signature or bytes differ)");
	}
	return true;
}

// Maps (cmap) are never magic (#1415, owner rule #1304): normal, rare or unique. The map TCs lean magic (Map Tier 1-3 Magic 800,
// champion and boss parents 1024) and itemtypes cannot refuse magic, so a magic map is stopped where it is made. D2R's item maker
// (0x442d60) sends quality 4 to the magic affix maker (0x442c60), and the magic fallback of a failed set, rare or unique roll
// ends there too. Refusing for a map (return 0 = "failed") makes the maker fall through to its own superior and normal makers,
// whatever rolled the quality: a forced quality, any call site, or Cartographer's roll hook being absent. Cartographer hooks
// the quality roll and the magic fallback instead (other addresses, so both hooks install); this one is the backstop.
// No PluginSDK service runs inside item creation. The maker is hooked as well, only to log each map's final quality.
using MagicMakerFn = uint64_t (*)(Unit**, void*) noexcept;
using ItemMakerFn = uint64_t (*)(void*, Unit**, void*) noexcept;
MagicMakerFn g_magicMakerOriginal = nullptr;
ItemMakerFn g_itemMakerOriginal = nullptr;
std::atomic<uint32_t> g_mapMagicRefused {0};
std::atomic<uint32_t> g_mapMade[10];

auto IsMapUnit(Unit* item) noexcept -> bool {
	return item != nullptr &&
	       d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(d2r::rva::ItemIsType)(item, d2r::rva::ItemTypeMap) != 0;
}

auto MapMagicMaker(Unit** item, void* request) noexcept -> uint64_t {
	if (item != nullptr && IsMapUnit(*item)) {
		if (plog::Sample(g_mapMagicRefused)) {
			plog::Info("map quality: a map was going to be magic, refused (%u so far)", g_mapMagicRefused.load());
		}
		return 0;
	}
	return g_magicMakerOriginal(item, request);
}

auto MapItemMaker(void* game, Unit** item, void* request) noexcept -> uint64_t {
	const uint64_t made = g_itemMakerOriginal(game, item, request);
	if (made != 0 && item != nullptr && IsMapUnit(*item)) {
		const int32_t quality = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::ItemQuality)(*item);
		const uint32_t n = ++g_mapMade[quality >= 0 && quality < 10 ? quality : 0];
		plog::Info("map quality: item maker made a map, quality %d (%u of that quality)", quality, n);
	}
	return made;
}

// Maps (cmap) stack only when both are normal quality and every stat but the quantity matches, layer by layer (#1326).
// Hooks D2Common's stack-equal check (no SDK service covers it): the drop-onto merge, ground pickup, vendor buys and
// the client's cursor guess all call it.
// D2Common Items.cpp #11102: (target, other) -> 1 when two items may stack; found at load by its signature
// (docs/research/maps/map-stacking.md).
using StackEqualFn = int32_t (*)(Unit*, Unit*) noexcept;
StackEqualFn g_stackEqualOriginal = nullptr;
std::atomic<uint32_t> g_mapStackRefused {0};

auto MapStackEqual(Unit* a, Unit* b) noexcept -> int32_t {
	using namespace d2r::rva;
	const auto isMap = [](Unit* u) {
		return u != nullptr && d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType)(u, ItemTypeMap) != 0 &&
		       d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality)(u) == QualityNormal;
	};
	if (isMap(a) && isMap(b)) {
		// Create writes -1 at item data +0x34 (the unique/set row) for a normal item but the save decoder does not, and the native
		// compare reads it: a fresh map would not stack with a loaded one (#1340). Not saved for normal items.
		const auto data = d2r::Engine<uint8_t* (*)(Unit*) noexcept>(UnitItemData);
		auto* const ra = reinterpret_cast<int32_t*>(data(a) + 0x34);
		auto* const rb = reinterpret_cast<int32_t*>(data(b) + 0x34);
		*ra = -1;
		*rb = -1;
	}
	const int32_t same = g_stackEqualOriginal(a, b);
	if (same == 0 || a == nullptr || b == nullptr ||
	    d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType)(a, ItemTypeMap) == 0) {
		return same;
	}
	const auto quality = d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality);
	const char* reason = "equal";
	int32_t stat = -1;
	if (quality(a) != QualityNormal || quality(b) != QualityNormal) {
		reason = "quality";
	} else if ((stat = pd2::mapstack::FirstDifference(
	                a, b, ItemStatCostRows, StatQuantity,
	                d2r::Engine<int32_t (*)(Unit*, uint32_t, pd2::mapstack::Entry*, int32_t) noexcept>(UnitStatEntries))) >= 0) {
		reason = "stat differs";
	}
	const bool merge = reason[0] == 'e';
	if (!merge) {
		++g_mapStackRefused;
	}
	// Each (a, b, caller) logs its first verdict only (#1340): the inventory scans call this once per map held, and the
	// server's call (caller image +0x4f2b3a) must not hide behind the client's
	static struct { Unit* a; Unit* b; uint64_t caller; uint32_t n; } seen[32];
	static uint32_t next = 0;
	const uint64_t caller = reinterpret_cast<uint64_t>(_ReturnAddress()) - d2r::imageBase;
	auto* slot = &seen[0];
	for (auto& s : seen) {
		if (s.a == a && s.b == b && s.caller == caller) {
			slot = &s;
			break;
		}
		slot = &seen[next % 32];
	}
	if (slot->a != a || slot->b != b || slot->caller != caller) {
		*slot = {a, b, caller, 0};
		++next;
	}
	// Outer cap: at most one line a second, so a full inventory scanned by two callers stays quiet whatever the ring holds
	static std::atomic<uint64_t> lastTick {0};
	const uint64_t now = GetTickCount64();
	if (++slot->n <= 1 && now - lastTick.load() >= 1000) {
		lastTick = now;
		const auto unitStat = d2r::Engine<int32_t (*)(Unit*, uint32_t, uint32_t) noexcept>(UnitStat);
		plog::Info("map stack: caller +0x%llx %08x q%d/%d quality %d/%d -> %s (%s %d)", static_cast<unsigned long long>(caller),
		           d2r::Engine<uint32_t (*)(Unit*) noexcept>(ItemCode)(a), unitStat(a, StatQuantity, 0),
		           unitStat(b, StatQuantity, 0), quality(a), quality(b), merge ? "merge" : "refused", reason, stat);
	}
	return merge ? 1 : 0;
}

// Progressive Affixes checks the magic affix maker's bytes when it loads and refuses if they changed. Our plugins load
// from the mod folder, core-rules.dll before d2rl-ruffneckk-progressive-affixes.dll (#1180), so this hook waits for
// the first table load, after every plugin's load-time hooks, whatever the load order.
// Runs after ReleaseReserve: its trampoline may take one freed block; 15+ stay for Cast Triggers' 5 relay pages (#1180)
void __cdecl HookMagicMaker(const D2RL::PluginContext* context, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	static bool tried = false;
	if (tried) {
		return;
	}
	tried = true;
	// 0x442c60 by signature (the loader patches its +0x18 load into a call, so those 6 bytes are masked); a whole-instruction entry
	static constexpr uint8_t MagicMakerEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
	const bool ok = sigscan::HookBySignature(
	    context, "MagicAffixMaker",
	    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 30 ?? ?? ?? ?? ?? ?? 45 33 FF 48 8B F2 48 8B F9", 0,
	    MagicMakerEntry, sizeof(MagicMakerEntry), MapMagicMaker, &g_magicMakerOriginal);
	if (ok) {
		plog::Info("no magic maps: magic affix maker hooked after the first table load");
	} else {
		plog::Warn("no magic maps: magic affix maker hook FAILED (signature or bytes differ)");
	}
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
	const bool weaponStrike = PatchWeaponCrit(context);
	const bool missileStrike = context->InstallInlineHook(MissileCritRoll, MissileCritRollEntry,
	                                                      sizeof(MissileCritRollEntry), MissileStrike, &g_missileRollOriginal);
	const bool blowRoll = PatchHitEvents(context);
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
	// The scheduler first: the handler alone would only see events that durability or quantity items schedule
	const bool replenish = context->InstallInlineHook(ScheduleReplenish, ScheduleReplenishEntry,
	                                                  sizeof(ScheduleReplenishEntry), ReplenishSchedule,
	                                                  &g_scheduleReplenishOriginal) &&
	                       context->InstallInlineHook(ReplenishEvent, ReplenishEventEntry, sizeof(ReplenishEventEntry),
	                                                  ChargesReplenishEvent, &g_replenishEventOriginal);
	// Unlimited books (IR5): D2R's item spell table is data; its free entry 17 gets the Town Portal handlers and 18
	// none (core-events' hooks identify all), both with keep 1, so the book stays after its use
	const bool unlimited = PatchUnlimitedBooks(context);
	const bool charms = InstallCharmRows(context);
	const char* const dye = InstallDyeTint(context);
	const bool mapStack = sigscan::HookBySignature(
	    context, "ItemsCanMerge", "48 89 5C 24 08 48 89 74 24 10 55 57 41 56 48 8B EC 48 83 EC 20", 0,
	    d2r::rva::ItemsCanMergeEntry, sizeof(d2r::rva::ItemsCanMergeEntry), MapStackEqual, &g_stackEqualOriginal);
	// 0x442d60 by signature (calls are masked); the entry is whole instructions. 0x442c60 waits for the tables (HookMagicMaker).
	static constexpr uint8_t ItemMakerEntry[] = {0x40, 0x53, 0x55, 0x41, 0x56, 0x48, 0x83, 0xec, 0x50, 0x4c, 0x8b, 0xf1, 0x49, 0x8b, 0xe8, 0x48, 0x8b, 0x0a};
	const D2RL::LifecycleService* lifecycle = nullptr;
	const D2RL::Lifecycle::DataTablesLoadedListener magicListener {.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
	                                                               .callback = HookMagicMaker};
	D2RL::Lifecycle::ListenerHandle magicHandle = D2RL::Lifecycle::InvalidHandle;
	const bool noMagicMaps = context->QueryService(&lifecycle) == D2RL::ServiceQueryResult::Success &&
	                         D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) &&
	                         lifecycle->registerDataTablesLoadedListener(context, &magicListener, &magicHandle) ==
	                             D2RL::Lifecycle::Result::Success;
	const bool mapMakerLog = sigscan::HookBySignature(
	    context, "ItemMaker", "40 53 55 41 56 48 83 EC 50 4C 8B F1 49 8B E8 48 8B 0A 48 8B DA 48 85 C9 74 0A E8 ?? ?? ?? ?? 83 F8 04 74 14", 0,
	    ItemMakerEntry, sizeof(ItemMakerEntry), MapItemMaker, &g_itemMakerOriginal);
	std::snprintf(report, reportSize, "property func %d ormus%s; weapon strike%s; missile strike%s; "
	              "crushing blow roll%s; life steal cap%s; crushing blow%s; curses%s; skill on equip and op bases%s; "
	              "replenish charges%s; unlimited books%s; charm rows%s; dye tint%s; map stacking%s; no magic maps%s; map maker log%s",
	              PropertySlot, property ? "" : " FAILED (slot not empty)",
	              weaponStrike ? "" : " FAILED (bytes differ)", missileStrike ? "" : " FAILED (bytes differ)",
	              blowRoll ? "" : " FAILED (call sites)", lifeCap ? "" : " FAILED (bytes differ)",
	              blow ? "" : " FAILED (slot write)", curses ? "" : " FAILED (bytes differ)",
	              equip ? "" : " FAILED (bytes differ)", replenish ? "" : " FAILED (bytes differ)",
	              unlimited ? "" : " FAILED (entries not empty, or no mutation service)",
	              charms ? "" : " FAILED (signature or bytes differ)", dye, mapStack ? "" : " FAILED (bytes differ)",
	              noMagicMaps ? " (hooked when tables load)" : " FAILED (no table listener)", mapMakerLog ? "" : " FAILED (signature or bytes differ)");
	return (property ? 1 : 0) + (weaponStrike ? 1 : 0) + (missileStrike ? 1 : 0) + (blowRoll ? 1 : 0) +
	       (lifeCap ? 1 : 0) + (blow ? 1 : 0) + (curses ? 1 : 0) + (equip ? 1 : 0) + (replenish ? 1 : 0) +
	       (unlimited ? 1 : 0) + (charms ? 1 : 0) + (dye[0] == 0 ? 1 : 0) + (mapStack ? 1 : 0) + (noMagicMaps ? 1 : 0) + (mapMakerLog ? 1 : 0);
}

auto ItemRuleCount() noexcept -> int {
	return 15;
}

auto ItemRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size,
	              "ormus rolls %u, strike rolls %u, crits %u, "
	              "deadly strikes %u, equipped_eth changes %u, life steal cap checks %u (blocked %u), crushing blow rolls %u, "
	              "crushing blows %u, curses %u, resisted %u, gated %u, skill on equip changes %u, ticks %u, phasing %u, "
	              "charges replenished %u, map stacks refused %u, magic maps refused %u",
	              g_rolls.load(), g_strikeRolls.load(), g_crits.load(), g_deadlyStrikes.load(), g_ethReads.load(),
	              g_capChecks.load(), g_capBlocks.load(), g_blowRolls.load(), g_blows.load(),
	              g_curses.load(), g_curseResisted.load(), g_curseGate.load(), g_equipCallbacks.load(), g_equipTicks.load(), g_phasing.load(),
	              g_chargesReplenished.load(), g_mapStackRefused.load(), g_mapMagicRefused.load());
}
