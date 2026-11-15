// The Horadric Almanac split out of map_open.cpp (#943), unchanged (almanac.h).
#include "almanac.h"

#include "log.h"

#include <atomic>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

// The Horadric Almanac (IR5, #240; core-rules' lane). map_open.cpp's item spell hooks call it: they own the dispatchers.
// PD2's unlimited Tome of Identify identifies every item at once and stays. Its item spell 18 has no D2R check or use
// (core-rules writes the entry, keep 1), so the hooks below run it. PD2's skill 513 handler (ProjectDiablo.dll
// 0x10383ea0) walks the player's inventory list and identifies each unidentified item on inventory page 0 or in the
// Cube (page 3), leaving the stash, worn and belt items; this does the same with D2R's identify (as its use function 1,
// 0x57ec30: sets the flag and sends the item to the client).
// Native, not the SDK's ItemInteraction + Inventory + executeExistingItemTransaction: ItemInteractionService raises a
// UI-thread event for the local player's own clicks, and the SDK maps no server player unit to a PlayerHandle, so a
// TCP/IP client's use (which reaches the host only as this item spell) could not be served; and on the test harness's
// right-click (Slot6, 2026-11-08) the interaction event was never raised while this item spell ran.
std::atomic<uint32_t> g_almanacUses {0};
constexpr int32_t UnitItem = 4;  // D2's unit type of an item

}  // namespace

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
	plog::Info("almanac: %d items identified, %d elsewhere left, %u uses", identified, left, ++g_almanacUses);
}
