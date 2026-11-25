// Critical and deadly strike, op bases and the life steal cap (IR3, docs/research/items/ir3-combat-stats.md).
// Moved verbatim from item_rules.cpp (#2198).
#include "shared.h"

namespace itemrules {

// --- Critical and deadly strike (IR3, docs/research/items/ir3-combat-stats.md) ---
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

// Counts every unit's rolls, players' too: Multiple Shot rolls once per arrow (#1609)
auto CountStrike(const Strike& s) noexcept -> void {
	++g_strikeRolls;
	if (s.kind == 1) {
		++g_crits;
	} else if (s.kind == 2) {
		++g_deadlyStrikes;
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
	CountStrike(s);
}

// D2R's missile roll (MissileCritRoll) with PD2's chances; the hit doubles, as in PD2, whose live missile fill
// (0x102fe730) rolls the same way and whose multiplier for missiles (0x103045a0) nothing calls
// (docs/research/items/ir3-combat-stats.md)
auto MissileStrike(Unit* unit, Unit* weapon) noexcept -> uint64_t {
	if (unit == nullptr) {
		return 0;
	}
	const Strike s = RollStrike(unit, weapon);
	CountStrike(s);
	return s.kind != 0 ? 1 : 0;
}

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
constexpr int32_t BodyLocFirst = 1, BodyLocLast = 10;  // worn; 11 and 12 hold the swapped weapons


std::atomic<uint32_t> g_ethReads {0};
std::atomic<uint32_t> g_missingHpRefreshes {0};
const D2RL::PluginContext* g_context = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
std::atomic<uint32_t> g_statRows[4] {};  // itemstatcost rows per data flag (the bank, as cube_rules reads it); 0 = unread

// Game thread. PD2's stats past D2R's 368 exist only in the RotW bank. A Classic or LoD character reads the base bank,
// and a stat id past it fails its extended save ("encoding-error=yes", #1825), so such a unit never gets one.
// shortcut: the count is read once per bank and not keyed on DataTablesLoadedEvent.revision; row counts are fixed per
// process, so it holds. Key it on the revision if a table reload ever changes a bank's row count.
auto InBank(Unit* unit, uint32_t stat) noexcept -> bool {
	const uint32_t flag = d2r::api.unitDataFlag(unit);
	if (g_tables == nullptr || flag >= std::size(g_statRows)) {
		return false;
	}
	uint32_t rows = g_statRows[flag].load(std::memory_order_relaxed);
	if (rows == 0) {
		D2RL::DataTables::TableView view {.structSize = D2RL::DataTables::TableViewSize};
		if (g_tables->getTable(g_context, static_cast<D2RL::DataTables::Bank>(flag), D2RL::DataTables::TableId::ItemStatCost,
		                       &view) != D2RL::DataTables::Result::Success) {
			static std::atomic<bool> warned {false};
			if (flag != 0 && !warned.exchange(true)) {
				plog::Warn("op bases: no itemstatcost view for data flag %u: equipped_eth and missing_hp are off there", flag);
			}
			return false;
		}
		rows = view.rowCount;
		g_statRows[flag].store(rows, std::memory_order_relaxed);
		plog::Info("op bases: data flag %u reads %u itemstatcost rows%s", flag, rows,
		           rows <= StatEquippedEth ? ": no equipped_eth or missing_hp there" : "");
	}
	return stat < rows;
}

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
	if ((type != 0 && type != 1) || !InBank(unit, stat)) {
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
	} else if (lists != 0 && type == 0) {
		const uint32_t n = ++g_missingHpRefreshes;
	}
}

// --- Life steal cap (IR3, Nightmare's Feast): lifedrain_percentcap, merged id 541 (PD2's 488) ---
constexpr uint32_t StatLifeDrainCap = 541;

std::atomic<uint32_t> g_capChecks {0};
std::atomic<uint32_t> g_capBlocks {0};

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
	return allowed ? g_addLifeOriginal(unit, amount) : 0;
}

}  // namespace itemrules
