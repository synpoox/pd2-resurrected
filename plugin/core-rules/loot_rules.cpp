// PD2's item generation and loot rules (IR8, #243):
//
// - the ethereal bonus: PD2's ApplyEthereal (ProjectDiablo.dll 0x102fb710) multiplies by 5/4 where D2R's (0x3d3510)
//   multiplies by 3/2: armor class on a non-weapon; min/max damage, two-handed and missile damage on a weapon.
//   No PluginSDK service sets the factor (ItemService's ethereal edit runs D2R's own 3/2), so D2R's function is
//   replaced at its entry. D2R's callers are kept as they are: the drop (0x442d60), the ethereal property
//   (0x3ccc60) and the cube output (0x5269c0), which calls it with the flag already set. So PD2's early return on an
//   item that is already ethereal is not ported: it would leave cube-made ethereal items with no bonus.
#include "loot_rules.h"

#include "d2r.h"
#include "log.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

using d2r::Unit;

constexpr int32_t TypeWeapon = 0x2d;            // itemtypes weap
constexpr uint32_t ItemFlagEthereal = 0x400000;
constexpr int32_t StatArmorClass = 31;
constexpr int32_t WeaponStats[] = {21, 22, 23, 24, 159, 160};  // mindamage, maxdamage, secondary_min/maxdamage, item_throw_min/maxdamage

std::atomic<uint32_t> g_ethereal {0};

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

}  // namespace

auto InstallLootRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	// Nothing calls through: PD2 replaces the whole function too
	const bool ethereal = context->InstallInlineHook(ItemApplyEthereal, ItemApplyEtherealEntry, sizeof(ItemApplyEtherealEntry),
	                                                 reinterpret_cast<void*>(&ApplyEthereal));
	std::snprintf(report, reportSize, "ethereal bonus%s", ethereal ? "" : " FAILED (bytes differ)");
	return ethereal ? 1 : 0;
}

auto LootRuleCount() noexcept -> int {
	return 1;
}

auto LootRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "ethereal bonuses %u", g_ethereal.load());
}
