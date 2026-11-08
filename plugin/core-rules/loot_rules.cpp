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
// - staffmods (#663): PD2's roll of class skills on a new class item (ProjectDiablo.dll 0x1036f470 on disk, 0x1036f400
//   in PD2.gpr) replaces 1.13c's (D2Game +0x13320 and +0x135e0) for every item version: a count from rand%100 + bonus,
//   each skill drawn from a fixed per-class list (knives their own) with a required-level test in place of 1.13c's
//   item-level tiers, and a level from rand%(90 + min(ilvl/3, 10)) + bonus/2. No PluginSDK service runs inside item
//   generation, so D2R's staffmods step (0x58a050) is wrapped: an item of the seven classes gets PD2's roll; a
//   Warlock item (PD2 has no list) keeps D2R's.
#include "loot_rules.h"

#include "pd2_engine.h"
#include "log.h"

#include <D2RLPlugin/mutations.h>

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
	if (n <= 20 || n % 100 == 0) {
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
	if (total <= 20 || total % 100 == 0) {
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

}  // namespace

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
	const bool staffMods = context->InstallInlineHook(ItemStaffMods, ItemStaffModsEntry, sizeof(ItemStaffModsEntry),
	                                                  &StaffMods, &g_staffModsOriginal);
	std::snprintf(report, reportSize, "ethereal bonus%s; affix rare%s; weapon durability 1/2%s; staffmods%s",
	              !decoder ? " FAILED (decoder bytes differ)" : ethereal ? "" : " FAILED (bytes differ)",
	              rare ? "" : " FAILED (bytes differ)", durability ? "" : " FAILED (bytes differ)",
	              staffMods ? "" : " FAILED (bytes differ)");
	return (ethereal ? 1 : 0) + (rare ? 1 : 0) + (durability ? 1 : 0) + (staffMods ? 1 : 0);
}

auto LootRuleCount() noexcept -> int {
	return 4;
}

auto LootRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "ethereal bonuses %u, rare-only affixes refused %u, staffmods rolls %u",
	              g_ethereal.load(), g_rareBlocked.load(), g_staffMods.load());
}
