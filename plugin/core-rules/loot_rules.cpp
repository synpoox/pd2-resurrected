// PD2's item generation and loot rules (IR8, #243):
//
// - the ethereal bonus: PD2's ApplyEthereal (ProjectDiablo.dll 0x102fb710) multiplies by 5/4 where D2R's (0x3d3510)
//   multiplies by 3/2: armor class on a non-weapon; min/max damage, two-handed and missile damage on a weapon.
//   No PluginSDK service sets the factor (ItemService's ethereal edit runs D2R's own 3/2), so D2R's function is
//   replaced at its entry. D2R's callers are kept as they are: the drop (0x442d60), the ethereal property
//   (0x3ccc60) and the cube output (0x5269c0), which calls it with the flag already set. So PD2's early return on an
//   item that is already ethereal is not ported: it would leave cube-made ethereal items with no bonus.
//   D2R's item decoder (0x378860) applies its own 3/2 to a weapon's damage each time it reads one (the client's copy,
//   so the tooltip, and every save load, so the real damage after a relog); those six multiplies are patched to 5/4.
// - an affix's `rare` (#254): PD2's picker (ProjectDiablo.dll 0x10389080, item quality from D2Common #10106) takes
//   0 on no rare, crafted or tempered item, 1 on any, 2 on no crafted or tempered item, 3 on rare items only. D2R's
//   pickers read the column as a flag (0x58c940), so PD2's rare-only affixes (Visceral, the +2 class skill helms, the
//   +3 skill trees) rolled on magic and crafted items. No SDK service filters an affix roll, so D2R's per-affix test
//   (0x3d4220), which both pickers call, is wrapped: it refuses 2 and 3 where PD2 does, then runs D2R's.
// - weapon durability: a weapon's chance to lose durability on a hit is halved (4% -> 2%, 10% -> 5%), as PD2's two
//   bytes over 1.13c's D2Game +0x12f16 / +0x12f36.
// - ethereal durability (#1478): a new ethereal item keeps its base's full maximum durability. D2R's item maker halves
//   the maximum (max / 2 + 1) after ApplyEthereal (0x44350c-0x443553); PD2 turns the `jz` that skips that block on an
//   item with no durability into a `jmp` (one byte, 0x74 -> 0xeb, 1.13c D2Game +0xec4f), so the block never runs.
//   Found by byte signature, patched through the MutationService. See docs/research/items/item-durability.md.
// - throwing quantity (#1489): a throwing stack keeps its quantity when its durability runs out. D2R's weapon durability
//   loss (0x441b10) takes one off the stack (`lea ebx,[rax-1]` at 0x441cb9) before it refills durability; PD2 NOPs the
//   `dec ebp` at 1.13c D2Game +0x13033 in the same branch (+0x12ee0). We skip the quantity write instead (see below).
//   Found by byte signature, patched through the MutationService.
// - unlimited ammo (#1598): arrows, bolts and throwing weapons never use up their quantity. D2R's ammo use (0x439130)
//   writes quantity - 1 back (`lea ebx,[rax-1]` at 0x43921d); PD2 NOPs the `dec ebp` at 1.13c D2Game +0xa16be in the
//   same function (+0xa16a0). We skip the quantity write instead (see below). Found by byte signature, patched through
//   the MutationService.
// - staffmods (#663): PD2's roll of class skills on a new class item (ProjectDiablo.dll 0x1036f470 on disk, 0x1036f400
//   in PD2.gpr) replaces 1.13c's (D2Game +0x13320 and +0x135e0) for every item version: a count from rand%100 + bonus,
//   each skill drawn from a fixed per-class list (knives their own) with a required-level test in place of 1.13c's
//   item-level tiers, and a level from rand%(90 + min(ilvl/3, 10)) + bonus/2. No PluginSDK service runs inside item
//   generation, so D2R's staffmods step (0x58a050) is wrapped: an item of the seven classes gets PD2's roll; a
//   Warlock item (PD2 has no list) keeps D2R's.
// - seal kills (#664): the monsters Chaos Sanctuary's sweep kills when the fifth seal opens drop nothing. PD2 sets
//   their no-treasure-class flag before 1.13c's kill (0x10372e10 over +0xbb4b8). No SDK service runs before a drop
//   (the monster-death event comes after it and only observes), so D2R's mode command (0x4471e0) is wrapped: called
//   from the sweep (its return address), it sets the flag on the monster first.
// - Armageddon Blade (#1843): an imbued Phase Blade (`7cr `) becomes the unique 2H Phase Blade (`7cr2`, the uniqueitems
//   row on it) 1 time in 280 / 140 / 70 (Normal / Nightmare / Hell). PD2 (ProjectDiablo.dll 0x10374c30, 0x10374ca0
//   installed) replaces Charsi's item-maker call (1.13c D2Game +0xc0422 in the NPC service +0xc00c0, NPC class 0x9a,
//   service 3: a rare at player level + 4 that keeps a personalized name); on a hit 0x10388a70 rewrites the drop
//   request to `7cr2`, unique, row 339 + 1, item level 87 and adds the nine properties by hand. Here the row holds the
//   nine (uniqueitems.txt, docs/deviations.md) and the same request rewrite runs on D2R's imbue: the ItemSpawn call in
//   PlrMsg's imbue panel handler (93847: 0x4fd750 in 0x4fc230, panel mode 1; 0x4fc230 is called from the packet
//   handlers 0x4ae280 and 0x4b6050, whose table entries 0x1d2a900 and 0x1d29fa8 point at them in the live image).
//   No PluginSDK service runs inside the imbue (or keeps its personalized name), so that one call is redirected by
//   signature; D2R's handler then personalizes and places the new item as it does the rare. A failed unique roll still
//   makes a rare `7cr2` (0x442d60 case 7): that item is freed and the stock request runs again. PD2 rolls the C
//   runtime's rand(), not a game or item seed: a plugin-wide D2 seed started from the CPU clock stands in.
//   `core-rules armageddon` (dev) swaps a Phase Blade in the inventory for the same unique through the SDK.
// Flying monsters' loot needs no port: PD2 patches nothing in 1.13c's drop placement, which, as D2R's (0x444680),
//   puts the item on the nearest free tile by mask 0x3e01, unwalkable tiles excluded.
#include "loot_rules.h"

#include "item_rules.h"
#include "pd2_engine.h"
#include "log.h"
#include "hot_log.h"
#include "sig.h"

#include <D2RLPlugin/data_tables.h>
#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/mutations.h>
#include <D2RLPlugin/threads.h>

#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {

using d2r::Unit;

constexpr int32_t TypeWeapon = 0x2d;            // itemtypes weap
constexpr uint32_t ItemFlagEthereal = 0x400000;
constexpr int32_t StatArmorClass = 31;
constexpr int32_t WeaponStats[] = {21, 22, 23, 24, 159, 160};  // mindamage, maxdamage, secondary_min/maxdamage, item_throw_min/maxdamage

std::atomic<uint32_t> g_ethereal {0};
std::atomic<uint32_t> g_rareBlocked {0};
std::atomic<uint32_t> g_staffMods {0};
std::atomic<uint32_t> g_sealKills {0};

using ModeCmdInitFn = uint64_t (*)(Unit*, int32_t, void*) noexcept;
ModeCmdInitFn g_modeCmdInitOriginal = nullptr;

auto SealSweepModeCmd(Unit* unit, int32_t mode, void* cmd) noexcept -> uint64_t {
	using namespace d2r::rva;
	if (unit != nullptr && reinterpret_cast<uint64_t>(_ReturnAddress()) == d2r::imageBase + SealSweepKillReturn) {
		d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(SetUnitFlags)(unit, UnitNoDrop, 1);
		++g_sealKills;
	}
	return g_modeCmdInitOriginal(unit, mode, cmd);
}

constexpr int32_t QualityRare = 6, QualityCrafted = 8, QualityTempered = 9;

using AffixAllowedFn = int32_t (*)(Unit*, const uint8_t*) noexcept;
AffixAllowedFn g_affixAllowedOriginal = nullptr;

// PD2's rule for `rare` 2 and 3 (0 and 1 D2R's picker already handles as PD2 does)
constexpr auto RareAllows(uint8_t rare, int32_t quality) noexcept -> bool {
	const bool crafted = quality == QualityCrafted || quality == QualityTempered;
	return rare == 3 ? quality == QualityRare : rare == 2 ? !crafted : true;
}
static_assert(RareAllows(3, QualityRare) && !RareAllows(3, 4) && !RareAllows(3, QualityCrafted));
static_assert(RareAllows(2, QualityRare) && RareAllows(2, 4) && !RareAllows(2, QualityCrafted) && RareAllows(1, 8));

auto RareAffixAllowed(Unit* item, const uint8_t* affix) noexcept -> int32_t {
	if (item != nullptr && affix != nullptr) {
		const uint8_t rare = affix[d2r::rva::AffixRare];
		if (rare >= 2) {
			const int32_t quality = d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::ItemQuality)(item);
			if (!RareAllows(rare, quality)) {
				++g_rareBlocked;
				return 0;
			}
		}
	}
	return g_affixAllowedOriginal(item, affix);
}

// PD2's rounding: (value * 5) / 4, truncated toward zero
constexpr auto Bonus(int32_t value) noexcept -> int32_t {
	return value * 5 / 4;
}
static_assert(Bonus(100) == 125 && Bonus(7) == 8 && Bonus(0) == 0);

auto Scale(Unit* item, int32_t stat) noexcept -> void {
	using namespace d2r::rva;
	const int32_t value = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitBaseStat)(item, stat, 0);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(item, stat, Bonus(value), 0);
}

// D2R's ApplyEthereal with PD2's factor
auto ApplyEthereal(Unit* item) noexcept -> void {
	using namespace d2r::rva;
	if (item == nullptr) {
		return;
	}
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(SetItemFlag)(item, ItemFlagEthereal, 1);
	const bool weapon = d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType)(item, TypeWeapon) != 0;
	const int32_t before = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitBaseStat)(
		item, weapon ? WeaponStats[1] : StatArmorClass, 0);
	if (weapon) {
		for (const int32_t stat : WeaponStats) {
			Scale(item, stat);
		}
	} else {
		Scale(item, StatArmorClass);
	}
	const uint32_t n = ++g_ethereal;
	if (pd2::HotSample(n)) {
		plog::Info("ethereal: %s %d -> %d (5/4), %u so far", weapon ? "max damage" : "armor class", before,
		           Bonus(before), n);
	}
}

// The decoder's own 3/2 (0x378860), which client tooltips and every save load use: 3/2 -> 5/4 in place, through the
// SDK's MutationService (no service sets the factor). The new bytes drop cdq's round toward zero: the base damage is
// a weapons.txt byte, never negative.
auto PatchDecoder(const D2RL::PluginContext* context) noexcept -> bool {
	using namespace d2r::rva;
	const D2RL::MutationService* mutations = nullptr;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize) ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		return false;
	}
	uint8_t expected[std::size(DecodeEtherealDamage)][16];
	uint8_t bytes[std::size(DecodeEtherealDamage)][16];
	for (size_t i = 0; i < std::size(DecodeEtherealDamage); ++i) {
		const auto stat = static_cast<uint8_t>(WeaponStats[i]);
		// lea eax,[rax+rax*2] / cdq / sub eax,edx / mov edx,stat / sar eax,1 / mov r8d,eax
		const uint8_t from[16] = {0x8d, 0x04, 0x40, 0x99, 0x2b, 0xc2, 0xba, stat, 0, 0, 0, 0xd1, 0xf8, 0x44, 0x8b, 0xc0};
		// lea r8d,[rax+rax*4] / sar r8d,2 / mov edx,stat / nop
		const uint8_t pd2[16] = {0x44, 0x8d, 0x04, 0x80, 0x41, 0xc1, 0xf8, 0x02, 0xba, stat, 0, 0, 0, 0x0f, 0x1f, 0x00};
		std::memcpy(expected[i], from, 16);
		std::memcpy(bytes[i], pd2, 16);
		const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
		                                              .rva = DecodeEtherealDamage[i],
		                                              .expected = expected[i],
		                                              .expectedSize = 16,
		                                              .bytes = bytes[i],
		                                              .size = 16};
		D2RL::Mutations::OperationHandle operation = 0;
		if (mutations->stageBytePatch(context, transaction, &patch, &operation) != D2RL::Mutations::Result::Success) {
			mutations->cancelTransaction(context, transaction);
			return false;
		}
	}
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		return false;
	}
	return true;
}

// Weapons lose durability at half D2R's rate: the two chance immediates in D2R's weapon durability loss (0x441b10),
// 4 -> 2 and 10 -> 5, as PD2 patches 1.13c's. Armor's loss is untouched (PD2 leaves it). No SDK service sets the
// chance, so it is a byte patch through the MutationService; `mov ebx,imm32` is checked whole.
auto PatchWeaponDurability(const D2RL::PluginContext* context) noexcept -> bool {
	using namespace d2r::rva;
	const D2RL::MutationService* mutations = nullptr;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize) ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		return false;
	}
	uint8_t expected[std::size(WeaponDurabilityChance)][5];
	uint8_t bytes[std::size(WeaponDurabilityChance)][5];
	for (size_t i = 0; i < std::size(WeaponDurabilityChance); ++i) {
		const uint8_t from[5] = {0xbb, WeaponDurabilityVanilla[i], 0, 0, 0};  // mov ebx,imm32
		const uint8_t pd2[5] = {0xbb, WeaponDurabilityPd2[i], 0, 0, 0};
		std::memcpy(expected[i], from, 5);
		std::memcpy(bytes[i], pd2, 5);
		const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
		                                              .rva = WeaponDurabilityChance[i],
		                                              .expected = expected[i],
		                                              .expectedSize = 5,
		                                              .bytes = bytes[i],
		                                              .size = 5};
		D2RL::Mutations::OperationHandle operation = 0;
		if (mutations->stageBytePatch(context, transaction, &patch, &operation) != D2RL::Mutations::Result::Success) {
			mutations->cancelTransaction(context, transaction);
			return false;
		}
	}
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		return false;
	}
	return true;
}

// Ethereal items keep full maximum durability: the `jz` (74 48) after D2R's "has durability" test in the item maker's
// ethereal block becomes `jmp` (eb 48), as PD2's 0x74 -> 0xeb at 1.13c D2Game +0xec4f. The pattern runs from the test's
// `test eax,eax` through the maximum halving (`sar eax,1; lea r8d,[rax+1]`); the offset points at the `jz`.
constexpr const char* EtherealDurabilityPattern =
	"85 C0 74 48 45 33 C0 48 8B CF 41 8D 50 49 E8 ?? ?? ?? ?? 99 45 33 C9 2B C2 48 8B CF D1 F8 41 8D 51 49 44 8D 40 01";
constexpr int EtherealDurabilityOffset = 2;
constexpr uint8_t EtherealJz[2] = {0x74, 0x48};
constexpr uint8_t EtherealJmp[2] = {0xeb, 0x48};

auto PatchEtherealDurability(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t rva = 0;
	const int matches = sigscan::ScanUnique(context->exeBase, EtherealDurabilityPattern, EtherealDurabilityOffset, &rva);
	if (matches != 1) {
		plog::Warn("skipped ethereal durability: %d matches", matches);
		return false;
	}
	plog::Info("found ethereal durability jz at 0x%llx", static_cast<unsigned long long>(rva));
	const D2RL::MutationService* mutations = nullptr;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize) ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		return false;
	}
	const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
	                                              .rva = rva,
	                                              .expected = EtherealJz,
	                                              .expectedSize = sizeof(EtherealJz),
	                                              .bytes = EtherealJmp,
	                                              .size = sizeof(EtherealJmp)};
	D2RL::Mutations::OperationHandle operation = 0;
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->stageBytePatch(context, transaction, &patch, &operation) != D2RL::Mutations::Result::Success ||
	    mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		return false;
	}
	return true;
}

// One byte patch found by a unique signature, through the MutationService; `name` is for the log.
auto PatchBySignature(const D2RL::PluginContext* context, const char* name, const char* pattern, int offset,
                      const uint8_t* from, const uint8_t* to, uint32_t size) noexcept -> bool {
	uint64_t rva = 0;
	const int matches = sigscan::ScanUnique(context->exeBase, pattern, offset, &rva);
	if (matches != 1) {
		plog::Warn("skipped %s: %d matches", name, matches);
		return false;
	}
	plog::Info("found %s at 0x%llx", name, static_cast<unsigned long long>(rva));
	const D2RL::MutationService* mutations = nullptr;
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (context->QueryService(&mutations) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasMutationServiceField(mutations, D2RL::MutationServiceRequiredSize) ||
	    mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		return false;
	}
	const D2RL::Mutations::BytePatchRequest patch{.structSize = D2RL::Mutations::BytePatchRequestSize,
	                                              .rva = rva,
	                                              .expected = from,
	                                              .expectedSize = size,
	                                              .bytes = to,
	                                              .size = size};
	D2RL::Mutations::OperationHandle operation = 0;
	D2RL::Mutations::CommitResult result{.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->stageBytePatch(context, transaction, &patch, &operation) != D2RL::Mutations::Result::Success ||
	    mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		return false;
	}
	return true;
}

// Both quantity patches skip the quantity write. Both reads (0x2f5020) return the full stat 70, which counts a
// socketed gem's own quantity 1 (gems are stackable), so writing the read back (PD2's way, 1.13c #10973 then #10887)
// grows a socketed stack by one per use (#1828, #1887).

// A throwing stack keeps its quantity when its durability runs out: in D2R's weapon durability loss (0x441b10), the
// stat 70 branch reads the quantity and sends quantity - 1 (`lea ebx,[rax-1]`, then 0x43eb30 with stat 0x46). The
// `lea` becomes a `jmp` to the durability refill (`mov rcx,rdi; call`, 0x21 bytes on), skipping the quantity write.
// The pattern runs from the branch's `jle` through that refill; the offset points at the `lea`.
constexpr const char* ThrowingQuantityPattern =
    "7E 57 33 F6 8D 58 FF E8 ?? ?? ?? ?? 48 8B D0 90 89 74 24 28 44 8D 4E 46 89 5C 24 20 41 B0 01 48 8B CF "
    "E8 ?? ?? ?? ?? 48 8B CF E8";
constexpr int ThrowingQuantityOffset = 4;
constexpr uint8_t ThrowingQuantityLea[3] = {0x8d, 0x58, 0xff};
constexpr uint8_t ThrowingQuantitySkip[3] = {0xeb, 0x21, 0x90};  // jmp +0x21; nop

// Arrows, bolts and throwing weapons never run out (#1598): D2R's ammo use (0x439130; its only caller 0x439320 passes
// the hand or quiver item) reads stat 70 and writes quantity - 1 back (`lea ebx,[rax-1]` at 0x43921d, set 0x2f7d10,
// packet 0x47a190), removing the item below 0. `lea ebx,[rax-1]` and the next REX byte become `xor ebx,ebx; jmp` past
// the set and the packet to the durability check (`mov rcx,rdi; call`, 0x42 bytes on); ebx 0 means "not used up".
// The pattern runs from the refill test (`test bl,bl; jnz`) through that check.
constexpr const char* AmmoQuantityPattern =
    "84 DB 0F 85 ?? ?? ?? ?? 8D 58 FF 45 8B FE 85 DB 41 8D 56 46 48 8B CF 44 0F 49 FB 45 33 C9 45 8B C7 C1 EB 1F "
    "E8 ?? ?? ?? ?? 48 8B CE E8 ?? ?? ?? ?? 48 8B C8 90 44 89 74 24 28 45 8D 4E 46 44 89 7C 24 20 41 B0 01 48 8B D7 "
    "E8 ?? ?? ?? ?? 48 8B CF E8";
constexpr int AmmoQuantityOffset = 8;
constexpr uint8_t AmmoQuantityLea[4] = {0x8d, 0x58, 0xff, 0x45};
constexpr uint8_t AmmoQuantitySkip[4] = {0x31, 0xdb, 0xeb, 0x42};  // xor ebx,ebx; jmp +0x42

// --- Staffmods ------------------------------------------------------------------------------------------------------

constexpr int32_t StatSingleSkill = 107;  // item_singleskill, layer = skill
constexpr int32_t TypeScepter = 24, TypeWand = 25, TypeKnife = 32;
constexpr int32_t QualityInferior = 1;
constexpr int32_t SkillPoisonDagger = 73, SkillHolySword = 116, SkillBladeDance = 452;  // merged (PD2 73, 116, 380)
constexpr uint32_t ClassCount = 7;  // Amazon to Assassin; list 7 is the knives'

// PD2's lists (ProjectDiablo.dll 0x105de988 in PD2.gpr, filled by its static initializer 0x10191c30), in merged
// skills.txt ids (merge/ids-skills.tsv; PD2's 364-383 moved to 436-455). The Barbarian's has no Blade, Mace or Spear
// Mastery (127, 129, 136); the knives' takes Poison Dagger and twelve Assassin skills.
constexpr int16_t ListAmazon[] = {6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                                  21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35};
constexpr int16_t ListSorceress[] = {36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,  51,  52,
                                     53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 441, 448, 455};
constexpr int16_t ListNecromancer[] = {66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80,  81,
                                       82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 439, 453};
constexpr int16_t ListPaladin[] = {96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112,
                                   113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 436, 443, 450};
constexpr int16_t ListBarbarian[] = {126, 128, 130, 131, 132, 133, 134, 135, 137, 138, 139, 140, 141, 142, 143,
                                     144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 440, 447};
constexpr int16_t ListDruid[] = {221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236,
                                 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 442};
constexpr int16_t ListAssassin[] = {251, 252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266,
                                    267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 438, 452};
constexpr int16_t ListKnives[] = {73, 254, 274, 259, 269, 265, 280, 255, 270, 275, 257, 266, 277};

struct SkillList {
	const int16_t* skills;
	int32_t count;
};
constexpr SkillList Lists[] = {{ListAmazon, static_cast<int32_t>(std::size(ListAmazon))},
                               {ListSorceress, static_cast<int32_t>(std::size(ListSorceress))},
                               {ListNecromancer, static_cast<int32_t>(std::size(ListNecromancer))},
                               {ListPaladin, static_cast<int32_t>(std::size(ListPaladin))},
                               {ListBarbarian, static_cast<int32_t>(std::size(ListBarbarian))},
                               {ListDruid, static_cast<int32_t>(std::size(ListDruid))},
                               {ListAssassin, static_cast<int32_t>(std::size(ListAssassin))},
                               {ListKnives, static_cast<int32_t>(std::size(ListKnives))}};
static_assert(std::size(ListAmazon) == 30 && std::size(ListSorceress) == 33 && std::size(ListNecromancer) == 32 &&
              std::size(ListPaladin) == 33 && std::size(ListBarbarian) == 29 && std::size(ListDruid) == 31 &&
              std::size(ListAssassin) == 32 && std::size(ListKnives) == 13);

// PD2's skill count from r = rand%100 + bonus: none below 10 without a bonus, 1 below 50, 2 below 80, else 3
constexpr auto StaffModsCount(uint32_t roll, int32_t bonus) noexcept -> int32_t {
	return roll >= 80 ? 3 : roll >= 50 ? 2 : (roll < 10 && bonus == 0) ? 0 : 1;
}
static_assert(StaffModsCount(9, 0) == 0 && StaffModsCount(9, 5) == 1 && StaffModsCount(49, 0) == 1 &&
              StaffModsCount(50, 0) == 2 && StaffModsCount(79, 0) == 2 && StaffModsCount(80, 0) == 3);

// PD2's skill level from r = rand%(90 + min(ilvl/3, 10)) + bonus/2: 1 below 40, 2 below 75, else 3
constexpr auto StaffModsLevel(uint32_t roll) noexcept -> int32_t {
	return roll >= 75 ? 3 : roll >= 40 ? 2 : 1;
}
static_assert(StaffModsLevel(39) == 1 && StaffModsLevel(40) == 2 && StaffModsLevel(74) == 2 &&
              StaffModsLevel(75) == 3);

using StaffModsFn = void (*)(Unit* item, const uint8_t* drop) noexcept;
StaffModsFn g_staffModsOriginal = nullptr;

auto Roll(Unit* item, int32_t n) noexcept -> uint32_t {
	using namespace d2r::rva;
	return d2r::RandomBelow(d2r::Engine<uint32_t* (*)(Unit*) noexcept>(ItemSeed)(item), static_cast<uint32_t>(n));
}

// skills.txt reqlevel, or -1 for a skill the table lacks
auto SkillReq(uint32_t dataFlag, int32_t skill) noexcept -> int32_t {
	using namespace d2r::rva;
	const uint8_t* record =
		d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(GetSkillRecord)(dataFlag, skill);
	return record == nullptr ? -1 : d2r::At<uint16_t>(record, SkReqLevel);
}

// item_singleskill on the item's magic list (flags 0x40), made and attached when it has none, as D2R's roll does
auto SetSingleSkill(Unit* item, uint32_t dataFlag, int32_t skill, int32_t level) noexcept -> void {
	using namespace d2r::rva;
	void* list = d2r::Engine<void* (*)(Unit*, int32_t, uint32_t) noexcept>(UnitListByFlags)(item, 0, ListFlagMagic);
	if (list == nullptr) {
		const int32_t id =
			d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitId)(item, __FILE__, __LINE__);
		list = d2r::Engine<void* (*)(int32_t, int32_t, int32_t, int32_t) noexcept>(NewStatList)(ListFlagMagic, 0, 4,
		                                                                                       id);
		if (list == nullptr) {
			return;
		}
		d2r::Engine<void (*)(Unit*, void*, int32_t) noexcept>(AttachStatList)(item, list, 1);
	}
	d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat)(dataFlag, list,
	                                                                                      StatSingleSkill, level, skill);
}

// PD2's roll (0x1036f400 in PD2.gpr), step for step, on the item's own seed
auto RollStaffMods(Unit* item, uint32_t list, int32_t ilvl, int32_t bonus) noexcept -> void {
	using namespace d2r::rva;
	const uint32_t total = ++g_staffMods;  // every roll PD2's function runs, the early-out ones too
	const int32_t quality = d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality)(item);
	if (ilvl == 1 && quality == QualityInferior) {
		return;
	}
	const int32_t count = StaffModsCount(Roll(item, 100) + static_cast<uint32_t>(bonus), bonus);
	if (count == 0) {
		return;
	}
	const uint32_t dataFlag = d2r::Engine<uint8_t (*)(Unit*) noexcept>(UnitDataFlag)(item);
	const auto isType = d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType);
	const bool wand = isType(item, TypeWand) != 0, scepter = isType(item, TypeScepter) != 0;
	const bool levelOne =
		d2r::Engine<uint16_t (*)(Unit*) noexcept>(ItemVersion)(item) >= 100 && quality == QualityInferior;
	const int32_t levelRange = 90 + (ilvl / 3 > 10 ? 10 : ilvl / 3);
	int32_t picked[3] = {-1, -1, -1};
	int32_t set = 0;
	for (int32_t n = 0; n < count; ++n) {
		for (int tries = 4; tries > 0; --tries) {
			const int32_t skill = Lists[list].skills[Roll(item, Lists[list].count)];
			const int32_t req = SkillReq(dataFlag, skill);
			const bool reachable = req == 1 || (req >= 0 && req + 7 <= ilvl);
			const bool barred = (skill == SkillPoisonDagger && wand) || (skill == SkillHolySword && scepter) ||
			                    skill == SkillBladeDance;
			if (reachable && !barred && skill != picked[0] && skill != picked[1] && skill != picked[2]) {
				picked[n] = skill;
				break;
			}
		}
		const int32_t level =
			levelOne ? 1 : StaffModsLevel(Roll(item, levelRange) + static_cast<uint32_t>(bonus / 2));
		if (picked[n] >= 0) {
			SetSingleSkill(item, dataFlag, picked[n], level);
			++set;
		}
	}
	if (pd2::HotSample(total)) {
		plog::Info("staffmods: list %u ilvl %d bonus %d: %d of %d skills (%d %d %d), %u so far", list, ilvl, bonus,
		           set, count, picked[0], picked[1], picked[2], total);
	}
}

auto StaffMods(Unit* item, const uint8_t* drop) noexcept -> void {
	using namespace d2r::rva;
	if (item == nullptr || drop == nullptr) {
		g_staffModsOriginal(item, drop);
		return;
	}
	// PD2's caller (1.13c +0x13de0) takes classes 0-6; D2R's 7 is the Warlock, which PD2 lacks: D2R's roll
	const uint32_t cls = d2r::Engine<uint32_t (*)(Unit*) noexcept>(StaffModsClass)(item);
	if (cls >= ClassCount) {
		g_staffModsOriginal(item, drop);
		return;
	}
	const bool knife = d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType)(item, TypeKnife) != 0;
	const int32_t ilvl = d2r::At<int32_t>(drop, DropItemLevel);
	// PD2's stubs: a version-0 item (1.13c +0x135e0) rolls with no bonus, else the item level when the drop asks
	const bool classic = d2r::Engine<uint16_t (*)(Unit*) noexcept>(ItemVersion)(item) == 0;
	const int32_t bonus = !classic && (d2r::At<uint32_t>(drop, DropFlags) & DropFlagStaffModsBonus) != 0 ? ilvl : 0;
	RollStaffMods(item, knife ? ClassCount : cls, ilvl, bonus);
}

// --- Armageddon Blade (#1843, top of file) ---
namespace items = D2RL::Items;
using d2r::Game;

constexpr uint32_t CodePhaseBlade = items::MakeItemCode("7cr"), CodeArmageddon = items::MakeItemCode("7cr2");
constexpr int32_t ArmageddonLevel = 87;               // PD2 0x10388a70: request +0x0c (1.13c) = 0x57
constexpr uint32_t ArmageddonOdds[] = {280, 140, 70};  // PD2 0x10374c30: rand() % odds < 1, by game +0x6d (1.13c)
constexpr int32_t QualityUnique = 7;
// D2R's drop request (200 bytes, filled for the imbue by 0x43dac0 from the old item): items row, quality, set or
// unique row + 1 (0x444680 passes it so; D2R's unique roll 0x443910 takes that row first, spawnable or not)
constexpr int DropItemRow = 0x20, DropQuality = 0x40, DropQualityRow = 0x50;
constexpr size_t DropRequestSize = 200;
// A uniqueitems record's base code (the SDK gives the rows and their size; no SDK field names it)
constexpr uint32_t UniqueCode = 0x28;
// The imbue frees the old item (93847: 0x43ec10 (game, item), `E8 rel32` at +10), then calls ItemSpawn (0x43cd50, at +30):
// the request at rbp+0x220, then PlrMsg's line 0x1908
constexpr const char* ImbueSpawnPattern =
	"48 8b 54 24 60 48 8b 4c 24 58 e8 ?? ?? ?? ?? "
	"48 8b 4c 24 58 48 8d 95 20 02 00 00 45 33 c0 e8 ?? ?? ?? ?? 48 89 44 24 60 48 85 c0 75 ?? 48 8d 4c 24 ?? 88 44 24 "
	"?? e8 ?? ?? ?? ?? 84 c0 74 01 cc 48 8b 44 24 60 41 b8 08 19 00 00";
constexpr uint32_t ImbueFreeCallAt = 10, ImbueSpawnCallAt = 30;

using ItemSpawnFn = Unit* (*)(Game* game, uint8_t* drop, int32_t flag) noexcept;
using ItemFreeFn = void (*)(Game* game, Unit* item) noexcept;
ItemSpawnFn g_imbueSpawnOriginal = nullptr;
ItemFreeFn g_itemFree = nullptr;
bool g_armageddonInstalled = false;
const char* g_armageddonOff = "the call redirect (MutationService)";  // the install report's reason when it is off
uint32_t g_imbueSeed[2] = {};  // PD2's rand() stand-in (top of file); the game thread alone rolls it
std::atomic<uint32_t> g_imbueRolls {0};
std::atomic<uint32_t> g_armageddons {0};
const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
const D2RL::DataTableService* g_tables = nullptr;

struct ArmageddonRows {
	int32_t phaseBlade = -1;  // items rows of 7cr and 7cr2
	int32_t blade = -1;
	int32_t unique = -1;      // the one uniqueitems row on 7cr2
};

// The bank's rows, read from its own tables through the SDK (game thread; every bank: one that lacks a row, or has two
// uniques on 7cr2, makes none)
auto FindArmageddon(uint32_t dataFlag, ArmageddonRows* rows) noexcept -> bool {
	namespace dt = D2RL::DataTables;
	const auto bank = static_cast<dt::Bank>(dataFlag);
	dt::RowView phase {.structSize = dt::RowViewSize};
	dt::RowView blade {.structSize = dt::RowViewSize};
	dt::TableView uniques {.structSize = dt::TableViewSize};
	rows->unique = -1;
	if (g_tables->findRowByCode(g_context, bank, dt::TableId::Items, CodePhaseBlade, &phase) != dt::Result::Success ||
	    g_tables->findRowByCode(g_context, bank, dt::TableId::Items, CodeArmageddon, &blade) != dt::Result::Success ||
	    g_tables->getTable(g_context, bank, dt::TableId::UniqueItems, &uniques) != dt::Result::Success ||
	    uniques.rows == nullptr || uniques.rowSize < UniqueCode + sizeof(uint32_t)) {
		return false;
	}
	rows->phaseBlade = static_cast<int32_t>(phase.rowIndex);
	rows->blade = static_cast<int32_t>(blade.rowIndex);
	const auto* records = static_cast<const uint8_t*>(uniques.rows);
	for (uint32_t i = 0; i < uniques.rowCount; ++i) {
		if (d2r::At<uint32_t>(records + static_cast<size_t>(i) * uniques.rowSize, UniqueCode) == CodeArmageddon) {
			if (rows->unique >= 0) {
				return false;
			}
			rows->unique = static_cast<int32_t>(i);
		}
	}
	return rows->unique >= 0;
}

// PD2's 0x10388a70 on D2R's request: base 7cr2, unique, its row, item level 87
auto MakeArmageddonRequest(uint8_t* drop, const ArmageddonRows& rows) noexcept -> void {
	*reinterpret_cast<int32_t*>(drop + DropItemRow) = rows.blade;
	*reinterpret_cast<int32_t*>(drop + DropQuality) = QualityUnique;
	*reinterpret_cast<int32_t*>(drop + DropQualityRow) = rows.unique + 1;
	*reinterpret_cast<int32_t*>(drop + d2r::rva::DropItemLevel) = ArmageddonLevel;
}

// The imbue's ItemSpawn (game thread). Any other base, a miss, or a failed unique: D2R's own imbue, untouched.
auto ImbueSpawn(Game* game, uint8_t* drop, int32_t flag) noexcept -> Unit* {
	ArmageddonRows rows;
	if (game == nullptr || drop == nullptr || !FindArmageddon(d2r::DataFlag(game), &rows) ||
	    d2r::At<int32_t>(drop, DropItemRow) != rows.phaseBlade) {
		return g_imbueSpawnOriginal(game, drop, flag);
	}
	const uint8_t difficulty = std::min<uint8_t>(d2r::At<uint8_t>(game, d2r::rva::GameDifficulty), 2);
	const uint32_t roll = d2r::RandomBelow(g_imbueSeed, ArmageddonOdds[difficulty]);
	const uint32_t rolls = ++g_imbueRolls;
	if (roll != 0) {
		plog::Info("armageddon: imbued Phase Blade, roll %u of %u missed (%u rolls)", roll, ArmageddonOdds[difficulty], rolls);
		return g_imbueSpawnOriginal(game, drop, flag);
	}
	uint8_t stock[DropRequestSize];
	std::memcpy(stock, drop, sizeof(stock));
	MakeArmageddonRequest(drop, rows);
	Unit* item = g_imbueSpawnOriginal(game, drop, flag);
	// A failed unique roll (0x442d60: once per game, or the row's version) still makes an item, a rare 7cr2: free it
	const int32_t quality = item != nullptr ? d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::ItemQuality)(item) : -1;
	if (quality == QualityUnique) {
		plog::Info("armageddon: imbued Phase Blade hit 1 in %u: 2H Phase Blade, unique row %d, item level %d (%u made)",
		           ArmageddonOdds[difficulty], rows.unique, ArmageddonLevel, ++g_armageddons);
		return item;
	}
	if (item != nullptr) {
		g_itemFree(game, item);
	}
	std::memcpy(drop, stock, sizeof(stock));
	plog::Warn("armageddon: the roll hit but the unique was not made (quality %d), the plain imbue goes on", quality);
	return g_imbueSpawnOriginal(game, drop, flag);
}

// --- `core-rules armageddon` (dev): the same unique from the same rows, by an SDK swap of a Phase Blade ---
struct Found {
	items::ItemInfo info {};
	bool found = false;
};

auto FindPhaseBlade(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* find = static_cast<Found*>(user);
	if (item->code != CodePhaseBlade) {
		return D2RL::Inventory::IterationAction::Continue;
	}
	find->info = *item;
	find->found = true;
	return D2RL::Inventory::IterationAction::Stop;
}

void ReadRows(const D2RL::PluginContext*, void* native, void* user) noexcept {
	auto* rows = static_cast<ArmageddonRows*>(user);
	if (!FindArmageddon(d2r::api.unitDataFlag(static_cast<Unit*>(native)), rows)) {
		rows->unique = -1;
	}
}

void ArmageddonNow(const D2RL::PluginContext*, void* user) noexcept {
	const auto player = static_cast<D2RL::PlayerHandle>(reinterpret_cast<uintptr_t>(user));
	Found old;
	const D2RL::Inventory::ItemFilter filter {.structSize = D2RL::Inventory::ItemFilterSize,
	                                          .containerMask = items::ContainerBit(items::ItemContainer::Inventory)};
	g_inventory->forEachInventoryItem(g_context, player, &filter, FindPhaseBlade, &old);
	ArmageddonRows rows;
	if (!old.found || g_items->editNativeItem(g_context, old.info.handle, ReadRows, &rows) != items::Result::Success ||
	    rows.unique < 0) {
		plog::Info("armageddon: console: %s", old.found ? "the bank has no 7cr, 7cr2 or one unique on 7cr2" : "no Phase Blade in the inventory");
		return;
	}
	const items::TransactionInput input {.item = old.info.handle, .quantity = 1, .socketedItemPolicy = items::SocketedItemPolicy::RejectIfNotEmpty};
	const items::ItemCreateSpec spec {.structSize = items::ItemCreateSpecSize,
	                                  .flags = items::ItemCreateFlagBit(items::ItemCreateFlag::AllowDuplicateUnique),  // repeatable
	                                  .code = CodeArmageddon,
	                                  .quality = items::Quality::Unique,
	                                  .qualityRecordId = static_cast<uint32_t>(rows.unique),
	                                  .itemLevel = ArmageddonLevel,
	                                  .seedMode = items::SeedMode::Random,
	                                  .quantity = items::DefaultValue,
	                                  .durability = items::DefaultValue,
	                                  .stateFlags = items::ItemStateIdentified,
	                                  .destination = {.structSize = items::ItemDestinationSize,
	                                                  .container = items::ItemContainer::Inventory,
	                                                  .placement = items::Placement::Automatic}};
	D2RL::ItemHandle made = D2RL::InvalidItemHandle;
	const items::Transaction transaction {.structSize = items::TransactionSize, .player = player, .inputCount = 1, .outputCount = 1,
	                                      .inputs = &input, .outputs = &spec, .outputItems = &made, .outputCapacity = 1};
	items::TransactionResult result {.structSize = items::TransactionResultSize};
	const items::Result status = g_items->executeTransaction(g_context, &transaction, &result);
	items::ItemInfo info {.structSize = items::ItemInfoSize};
	const bool read = status == items::Result::Success && g_items->getItemInfo(g_context, made, &info) == items::Result::Success;
	plog::Info("armageddon: console: Phase Blade (class %u, bank row %d) -> 2H Phase Blade row %d: result %u, quality %u, "
	           "unique row %d, item level %u",
	           old.info.classId, rows.phaseBlade, rows.blade, static_cast<uint32_t>(status), read ? static_cast<uint32_t>(info.quality) : 0,
	           read ? info.qualityRecordId : -1, read ? info.itemLevel : 0);
}

// UI thread: the SDK names the local player here
void ArmageddonUi(const D2RL::PluginContext*, void*) noexcept {
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success ||
	    g_threads->runOnGameThread(g_context, ArmageddonNow, reinterpret_cast<void*>(static_cast<uintptr_t>(player))) !=
	        D2RL::Threads::Result::Success) {
		plog::Info("armageddon: console: no local player, or the game thread refused the job");
	}
}

template <class Service>
auto Query(const D2RL::PluginContext* context, const Service*& service, bool (*has)(const Service*, uint32_t),
           uint32_t required) noexcept -> bool {
	if (context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !has(service, required)) {
		service = nullptr;
	}
	return service != nullptr;
}

auto InstallArmageddon(const D2RL::PluginContext* context) noexcept -> bool {
	g_context = context;
	Query(context, g_items, D2RL::HasItemServiceField, D2RL::ItemServiceRequiredSize);
	Query(context, g_inventory, D2RL::HasInventoryServiceField, D2RL::InventoryServiceRequiredSize);
	Query(context, g_threads, D2RL::HasThreadServiceField, D2RL::ThreadServiceRequiredSize);
	Query(context, g_tables, D2RL::HasDataTableServiceField, D2RL::DataTableServiceRequiredSize);
	const uint64_t clock = __rdtsc();
	g_imbueSeed[0] = static_cast<uint32_t>(clock) | 1;
	g_imbueSeed[1] = static_cast<uint32_t>(clock >> 32);
	// The free item call from the same match (no shared entry names 0x43ec10)
	uint64_t match = 0;
	const int matches = sigscan::ScanUnique(context->exeBase, ImbueSpawnPattern, 0, &match);
	const auto* freeCall = reinterpret_cast<const uint8_t*>(context->exeBase + match + ImbueFreeCallAt);
	const char* missing = g_tables == nullptr                                ? "the SDK DataTable service"
	                    : d2r::sig::ItemQuality.state != sigscan::State::Ok ? "signature ItemQuality"
	                    : matches != 1                                       ? "the imbue call pattern (not one match)"
	                    : freeCall[0] != 0xe8                                ? "the free item call"
	                                                                         : nullptr;
	if (missing != nullptr) {
		g_armageddonOff = missing;
		return false;
	}
	int32_t rel = 0;
	std::memcpy(&rel, freeCall + 1, sizeof(rel));
	g_itemFree = reinterpret_cast<ItemFreeFn>(freeCall + 5 + rel);
	g_armageddonInstalled = RedirectCall(context, ImbueSpawnPattern, ImbueSpawnCallAt, reinterpret_cast<uint64_t>(&ImbueSpawn),
	                                     reinterpret_cast<uint64_t*>(&g_imbueSpawnOriginal));
	return g_armageddonInstalled;
}

}  // namespace

auto QueueArmageddon(char* why, size_t whySize) noexcept -> bool {
	const char* missing = !g_armageddonInstalled || d2r::api.unitDataFlag == nullptr
	                        ? "the imbue rule is not installed"
	                    : g_items == nullptr || g_inventory == nullptr || g_threads == nullptr
	                        ? "the SDK item, inventory or thread service is missing"
	                    : g_threads->runOnUiThread(g_context, ArmageddonUi, nullptr) != D2RL::Threads::Result::Success
	                        ? "could not queue it on the UI thread"
	                        : nullptr;
	std::snprintf(why, whySize, "%s", missing != nullptr ? missing : "queued, see the log");
	return missing == nullptr;
}

auto InstallLootRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	// The hook only after the decoder patch: a new item at 5/4 that reloads at 3/2 is worse than 3/2 throughout.
	// Nothing calls through: PD2 replaces the whole function too
	const bool decoder = PatchDecoder(context);
	const bool ethereal = decoder && context->InstallInlineHook(ItemApplyEthereal, ItemApplyEtherealEntry,
	                                                            sizeof(ItemApplyEtherealEntry),
	                                                            reinterpret_cast<void*>(&ApplyEthereal));
	const bool rare = context->InstallInlineHook(AffixAllowed, AffixAllowedEntry, sizeof(AffixAllowedEntry),
	                                             &RareAffixAllowed, &g_affixAllowedOriginal);
	const bool durability = PatchWeaponDurability(context);
	const bool etherealDurability = PatchEtherealDurability(context);
	const bool throwing = PatchBySignature(context, "throwing quantity lea", ThrowingQuantityPattern,
	                                       ThrowingQuantityOffset, ThrowingQuantityLea, ThrowingQuantitySkip,
	                                       sizeof(ThrowingQuantityLea));
	const bool ammo = PatchBySignature(context, "ammo quantity lea", AmmoQuantityPattern, AmmoQuantityOffset,
	                                   AmmoQuantityLea, AmmoQuantitySkip, sizeof(AmmoQuantityLea));
	const bool staffMods = context->InstallInlineHook(ItemStaffMods, ItemStaffModsEntry, sizeof(ItemStaffModsEntry),
	                                                  &StaffMods, &g_staffModsOriginal);
	const bool seals = context->InstallInlineHook(ModeCmdInit, ModeCmdInitEntry, sizeof(ModeCmdInitEntry),
	                                              &SealSweepModeCmd, &g_modeCmdInitOriginal);
	const bool armageddon = InstallArmageddon(context);
	std::snprintf(report, reportSize,
	              "ethereal bonus%s; affix rare%s; weapon durability 1/2%s; ethereal durability%s; throwing quantity%s; unlimited ammo%s; staffmods%s; seal kills drop nothing%s; armageddon imbue%s%s%s",
	              !decoder ? " FAILED (decoder bytes differ)" : ethereal ? "" : " FAILED (bytes differ)",
	              rare ? "" : " FAILED (bytes differ)", durability ? "" : " FAILED (bytes differ)",
	              etherealDurability ? "" : " FAILED (signature or bytes differ)",
	              throwing ? "" : " FAILED (signature or bytes differ)",
	              ammo ? "" : " FAILED (signature or bytes differ)",
	              staffMods ? "" : " FAILED (bytes differ)", seals ? "" : " FAILED (bytes differ)",
	              armageddon ? "" : " FAILED (", armageddon ? "" : g_armageddonOff, armageddon ? "" : ")");
	return (ethereal ? 1 : 0) + (rare ? 1 : 0) + (durability ? 1 : 0) + (etherealDurability ? 1 : 0) + (throwing ? 1 : 0) + (ammo ? 1 : 0) + (staffMods ? 1 : 0) + (seals ? 1 : 0) + (armageddon ? 1 : 0);
}

auto LootRuleCount() noexcept -> int {
	return 9;
}

auto LootRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size,
	              "ethereal bonuses %u, rare-only affixes refused %u, staffmods rolls %u, seal kills without drops %u, "
	              "armageddon imbue rolls %u (hits %u)",
	              g_ethereal.load(), g_rareBlocked.load(), g_staffMods.load(), g_sealKills.load(), g_imbueRolls.load(),
	              g_armageddons.load());
}
