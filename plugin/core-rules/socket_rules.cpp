// Socketed gems and runes apply their gems.txt mods once (#450).
//
// Our merged misc.txt gives every gem and rune compactsave=0 (PD2's stacks need it). D2R then keeps the filler's own
// stat list as well as applying gems.txt, so the mods count twice on insertion and a third time after a reload.
// A socketed filler is always one item and never comes out, so after its mods are applied it gets the item flag
// compact (0x200000): the save and the item packet then write it without stats, as vanilla's compactsave=1 does.
// The SDK has no service for item flags or this function, hence the native hook (pd2_engine.h).
#include "socket_rules.h"

#include "pd2_engine.h"
#include "log.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Unit;

std::atomic<uint32_t> g_marked {0};

using SocketModsFn = void (*)(uint8_t dataFlag, Unit* filler, Unit* parent, int32_t arg) noexcept;
SocketModsFn g_original = nullptr;

auto IsItem(Unit* unit) noexcept -> bool {
	return unit != nullptr && At<int32_t>(unit, d2r::rva::UnitType) == 4;
}

void SocketMods(uint8_t dataFlag, Unit* filler, Unit* parent, int32_t arg) noexcept {
	g_original(dataFlag, filler, parent, arg);
	// Only an item parent: a jewel (its own rolled stats) never matches the type test.
	if (IsItem(parent)) {
		MarkSocketedCompact(filler);
	}
}

}  // namespace

auto MarkSocketedCompact(Unit* filler) noexcept -> bool {
	using namespace d2r::rva;
	if (!IsItem(filler)) {
		return false;
	}
	const auto isType = d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType);
	if (isType(filler, ItemTypeGem) == 0 && isType(filler, ItemTypeRune) == 0) {
		return false;
	}
	auto* data = d2r::Engine<uint8_t* (*)(Unit*) noexcept>(UnitItemData)(filler);
	if (data == nullptr) {
		return false;
	}
	auto& flags = *reinterpret_cast<uint32_t*>(data + ItemDataFlags);
	if ((flags & ItemFlagCompactSave) != 0) {
		return false;
	}
	flags |= ItemFlagCompactSave;
	plog::Sample(g_marked);
	return true;
}

auto InstallSocketRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	const bool hook = context->InstallInlineHook(SocketFillerMods, SocketFillerModsEntry, sizeof(SocketFillerModsEntry),
	                                             SocketMods, &g_original);
	std::snprintf(report, reportSize, "socket mods once%s", hook ? "" : " FAILED (bytes differ)");
	return hook ? 1 : 0;
}

auto SocketRuleCount() noexcept -> int {
	return 1;
}

auto SocketRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "socketed fillers marked compact %u", g_marked.load());
}
