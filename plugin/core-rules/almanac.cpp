// The Horadric Almanac (IR5, #240), in core-rules since #945. PD2's unlimited Tome of Identify (`rid`) identifies every
// unidentified item on inventory page 0 and in the Cube at once, and leaves the stash, worn and belt items.
//
// Trigger: D2R's own item spell path. A right-click sends the use message, the server's use-item handler 0x4f40c0 runs
// the item's spell entry, and the dispatcher 0x581680 calls the use handler its entry names. Entry 18 (item_rules.cpp)
// shares use slot 14 with the deciphered Scroll of Inifuss. The table patch keeps Cartographer's dispatchers intact.
// The SDK's ItemInteractionService raised no event for this right-click (Slot4 2026-11-15). This handler runs on the
// game thread for any player, including TCP/IP clients.
// PD2's skill 513 handler (ProjectDiablo.dll 0x10383ea0) walks the player's inventory list the same way; this identifies
// with D2R's identify (use function 1, 0x57ec30: sets the flag and sends the item to the client).
#include "almanac.h"

#include "log.h"
#include "pd2_engine.h"

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

std::atomic<uint32_t> g_uses {0};
constexpr int32_t UnitItem = 4;  // D2's unit type of an item

void IdentifyAll(Game* game, Unit* player) noexcept {
	using namespace d2r::rva;
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory)(player, __FILE__, __LINE__);
	if (inventory == nullptr) {
		return;
	}
	const auto next = d2r::Engine<Unit* (*)(Unit*) noexcept>(InventoryNextItem);
	const auto page = d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemPage);
	const auto hasFlag = d2r::Engine<int32_t (*)(Unit*, uint32_t, int32_t, const char*) noexcept>(ItemTestFlag);
	const auto identify = d2r::Engine<void (*)(Game*, Unit*, Unit*, int32_t) noexcept>(IdentifyItem);
	int32_t identified = 0, left = 0;
	for (Unit* item = d2r::Engine<Unit* (*)(void*) noexcept>(InventoryFirstItem)(inventory); item != nullptr;
	     item = next(item)) {
		if (At<int32_t>(item, UnitType) != UnitItem || hasFlag(item, ItemFlagIdentified, __LINE__, __FILE__) != 0) {
			continue;
		}
		const uint8_t where = page(item);
		if (where != 0 && where != CubePageCube) {
			++left;
			continue;
		}
		identify(game, player, item, 1);
		++identified;
	}
	plog::Info("almanac: use handler ran, %d items identified, %d elsewhere left, %u uses", identified, left, ++g_uses);
}

// The stock handler sends the current game's Cairn Stone order to the reader (#1538).
auto UseAlmanac(Game* game, Unit* player, Unit* item) noexcept -> uint8_t {
	using namespace d2r::rva;
	const auto itemCode = d2r::Engine<uint32_t (*)(Unit*) noexcept>(ItemCode);
	if (item == nullptr || itemCode(item) != D2RL::Items::MakeItemCode("rid")) {
		return d2r::Engine<uint8_t (*)(Game*, Unit*, Unit*) noexcept>(AlmanacUseSlotOriginal)(game, player, item);
	}

	const uint8_t where = d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemPage)(item);
	if (where == 0) {
		IdentifyAll(game, player);
	} else {
		plog::Info("almanac: use refused, the Almanac sits on page %u", where);
	}
	return 1;
}

}  // namespace

auto InstallAlmanac(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	const uint64_t expected = d2r::imageBase + AlmanacUseSlotOriginal;
	const bool live = context->PatchWriteU64(ItemSpellUseTable + static_cast<uint64_t>(AlmanacUseSlot) * 8, &expected,
	                                         sizeof(expected), reinterpret_cast<uint64_t>(&UseAlmanac));
	std::snprintf(report, reportSize, "almanac identify-all, use slot %d%s", AlmanacUseSlot,
	              live ? "" : " FAILED (the slot differs)");
	return live ? 1 : 0;
}

auto AlmanacCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "almanac uses %u", g_uses.load());
}
