// Merging a quick-moved stack into its cell's stack, and the map stack rules.
// Moved verbatim from stash_affinity.cpp (#2198).
#include "shared.h"

namespace stash_affinity {

namespace detail {

// A map's non-quantity stats (Area Level, forced event, mods), read off the native item
void CollectMapStats(const D2RL::PluginContext*, void* native, void* user) noexcept {
	pd2::mapstack::Collect(static_cast<d2r::Unit*>(native), d2r::rva::ItemStatCostRows, d2r::rva::StatQuantity,
	                       d2r::Engine<int32_t (*)(d2r::Unit*, uint32_t, pd2::mapstack::Entry*, int32_t) noexcept>(d2r::rva::UnitStatEntries),
	                       *static_cast<pd2::mapstack::Saved*>(user));
}

// Core-rules' rule (#1326): two maps stack only when both are normal quality with equal non-quantity stats, or a rare's
// mods would go into the stack. nullptr when they may, else the reason; shared by Route (MapRefusal) and the hand stack.
// same is 1 or 0 once the stats were compared, else -1.
auto MapStatsRefusal(const items::ItemInfo& a, const items::ItemInfo& b, int32_t& same) noexcept -> const char* {
	static pd2::mapstack::Saved sa, sb;  // game thread only; too big for the stack
	same = -1;
	if (a.quality != items::Quality::Normal || b.quality != items::Quality::Normal) {
		return "quality";
	}
	if (g_items->editNativeItem(g_context, a.handle, CollectMapStats, &sa) != items::Result::Success ||
	    g_items->editNativeItem(g_context, b.handle, CollectMapStats, &sb) != items::Result::Success) {
		return "stats unread";
	}
	same = pd2::mapstack::SameStats(sa, sb) ? 1 : 0;
	return same == 1 ? nullptr : "stats";
}

// Route's merge test: the stack on its page and cell, a sum that fits, then MapStatsRefusal.
// nullptr when the pair stacks, else the reason it does not; one log line per candidate keeps the decision readable.
auto MapRefusal(const items::ItemInfo& a, const items::ItemInfo& b, uint32_t stackPage, bool inView, bool shown) noexcept -> const char* {
	const char* reason = nullptr;
	int32_t same = -1;
	if (b.sharedStashPage != stackPage || !InCell(b.x, b.y) || (shown && !inView)) {
		reason = "page";
	} else if (a.quality != items::Quality::Normal || b.quality != items::Quality::Normal) {
		reason = "quality";
	} else if (Quantity(b.handle) + Quantity(a.handle) > aff::StackCap(a.code)) {
		reason = "count";
	} else {
		reason = MapStatsRefusal(a, b, same);
	}
	return reason;
}

// The hand stack's test (#1758): any other stackable always; a map by MapStatsRefusal
auto StackAllowed(const items::ItemInfo& a, const items::ItemInfo& b) noexcept -> bool {
	int32_t same = -1;
	return !pd2::mapstack::IsMapCode(a.code) || MapStatsRefusal(a, b, same) == nullptr;
}

// Into the same currency's stack on its cell; Conflict when the sum passes a stack. editItem and destroyItem refuse a
// shared-stash item on this loader (Unsupported, Slot2), so both go to the inventory, the counts are read there, the
// stack takes the sum, the incoming item goes and the stack moves back onto its cell. A failed step puts things back;
// the sum is never doubled. topUp (a hand drop #1758, a fixed cell's Route #1771): the stack takes what fits up to MaxStack and the rest stays on the
// cursor item, as D2R's own stack message does (0x4c1940). The counts are checked before anything moves: a full stack
// refuses with no change (D2R refuses that message silently too), since the stack's round trip with nothing sent for the
// cursor item left it invisible on the client (#1758).
auto Merge(D2RL::PlayerHandle player, items::ItemInfo& item, items::ItemInfo& stack, bool topUp) noexcept -> items::Result {
	const items::ItemDestination bag {.structSize = items::ItemDestinationSize, .container = C::Inventory,
	                                  .placement = items::Placement::Automatic};
	const uint32_t page = stack.sharedStashPage;
	const int32_t x = stack.x, y = stack.y;
	const int32_t MaxStack = aff::StackCap(stack.code);  // 255, or 50 for a map (#1763)
	// an unread count (0) is left to the read after the move below
	if (const int32_t was = Quantity(stack.handle), more = Quantity(item.handle);
	    was > 0 && more > 0 && (topUp ? aff::ToppedUp(was, more, MaxStack).stack : aff::MergedCount(was, more, MaxStack)) == 0) {
		return items::Result::Conflict;
	}
	if (const auto out = Move(player, stack.handle, bag); out != items::Result::Success) {
		return out;
	}
	stack.container = C::Inventory;  // so the move back keeps the cell noted as this stack's
	// The incoming item out first: with a full inventory the take fails before any quantity changed
	const char* step = "take";
	auto result = items::Result::Success;
	if (item.container == C::SharedStash) {
		if (result = Move(player, item.handle, bag); result == items::Result::Success) {
			if (Reserved(item) && InCell(item.x, item.y)) {
				Use(item.sharedStashPage, item.x, item.y, false);
				if (item.sharedStashPage == aff::MapsPage || item.sharedStashPage == aff::BossingPage) {
					At(item.sharedStashPage, item.y, item.x) = -1;
				}
			}
			item.container = C::Inventory;
		}
	}
	int32_t have = 0, add = 0, sum = 0, rest = 0;
	if (result == items::Result::Success) {
		step = "count";
		have = Quantity(stack.handle);
		add = Quantity(item.handle);
		const aff::TopUp up = topUp ? aff::ToppedUp(have, add, MaxStack) : aff::TopUp {aff::MergedCount(have, add, MaxStack), 0};
		sum = up.stack;
		rest = up.rest;
		if (sum == 0) {  // a full stack (Conflict) or an unread count (Unsupported): the edit would lose items
			result = have < 1 || add < 1 ? items::Result::Unsupported : items::Result::Conflict;
		}
	}
	if (result == items::Result::Success) {
		// the loader misreports these items' quantity (#1628): SetQuantity trusts the stat, not the edit's result, before destroying
		step = "edit";
		if (result = SetQuantity(player, stack.handle, sum); result != items::Result::Success) {
			SetQuantity(player, stack.handle, have);
		}
		if (result == items::Result::Success) {
			if (rest > 0) {  // the rest stays on the cursor item, verified as the stack is
				step = "rest";
				if (result = SetQuantity(player, item.handle, rest); result != items::Result::Success) {
					SetQuantity(player, item.handle, add);
				}
			} else {
				step = "destroy";
				result = g_items->destroyItem(g_context, player, item.handle, items::SocketedItemPolicy::RejectIfNotEmpty);
			}
			if (result != items::Result::Success) {
				// undo, so nothing is made from nothing
				if (const auto undo = SetQuantity(player, stack.handle, have); undo != items::Result::Success) {
					plog::Warn("affinity: %08x undo failed (result %u): the stack may hold %d with the item still there", item.code,
					           static_cast<uint32_t>(undo), sum);
				}
			}
		}
	}
	const auto back = MoveTo(player, stack, page, x, y);  // MoveTo notes the cell used again
	if (result != items::Result::Success) {
		return result;  // Route sends the incoming item back to the inventory
	}
	if (rest == 0) {
		item.code = 0;  // gone: no later step of this pass reads it
	}
	stack.quantity = sum;
	++g_merged;
	return items::Result::Success;  // a failed move back leaves the whole stack in the inventory: nothing lost
}

auto FindStored(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* find = static_cast<Find*>(user);
	if (item->runtimeId != find->id) {
		return D2RL::Inventory::IterationAction::Continue;
	}
	find->info = *item;
	return D2RL::Inventory::IterationAction::Stop;
}

constexpr D2RL::Inventory::ItemFilter Carried {.structSize = D2RL::Inventory::ItemFilterSize,
                                               .containerMask = items::ContainerBit(C::Inventory) | items::ContainerBit(C::Cube)};

// The client's copy of the cursor item after a consumed 0x21 (#1762). Sending 0x21 marks it pending (0x1c6ac0, state
// 0x36), and the client then ignores clicks with it (0x2c81d0 returns on 0x1c7360) until an item packet for it arrives
// (0x1c6a40). A merge's stack move also redraws the client's cursor: the stack's 0x9d action 5 (remove from container,
// 0x1c60a0) makes the stack the client's cursor item (0x389990) and action 4 puts it down, so the rest left on the
// cursor was not drawn though the server held it. An editItem of the cursor item to its own count, after the last move,
// sends the cursor item again (0x9d action 0x15, 0x1c6540: the client rebuilds it and makes it the cursor item) without
// changing it. No-op when the cursor item is gone (a whole merge destroyed it). #1766: a refused hand drop or quick-move
// leaves its item pending the same way; a quick-move's item is still in the inventory or Cube, and an item that does not
// stack (count 0) is edited to its own identified state instead.
void ResendCursor(D2RL::PlayerHandle player, uint32_t cursorId) noexcept {
	D2RL::ItemHandle held {};
	items::ItemInfo item {.structSize = items::ItemInfoSize};
	if (g_inventory->getCursorItem(g_context, player, &held) != D2RL::Inventory::Result::Success ||
	    g_items->getItemInfo(g_context, held, &item) != items::Result::Success || item.runtimeId != cursorId) {
		Find stored {.id = cursorId};
		g_inventory->forEachInventoryItem(g_context, player, &Carried, FindStored, &stored);
		if (stored.info.runtimeId != cursorId || cursorId == 0) {
			return;
		}
		item = stored.info;
		held = item.handle;
	}
	const int32_t count = Quantity(held);
	const uint32_t identified = item.stateFlags & items::ItemStateIdentified;
	const items::ItemEdit same = count > 0 ? items::ItemEdit {.structSize = items::ItemEditSize,
	                                                          .fields = items::EditFieldBit(items::EditField::Quantity),
	                                                          .quantity = static_cast<uint32_t>(count)}
	                                       : items::ItemEdit {.structSize = items::ItemEditSize,
	                                                          .fields = items::EditFieldBit(items::EditField::Identified),
	                                                          .stateFlags = identified};
	const auto sent = g_items->editItem(g_context, player, held, &same);
	// self-check: the resend changes nothing on the server
	items::ItemInfo after {.structSize = items::ItemInfoSize};
	const bool kept = Quantity(held) == count && g_items->getItemInfo(g_context, held, &after) == items::Result::Success &&
	                  (after.stateFlags & items::ItemStateIdentified) == identified;
	if (sent != items::Result::Success || !kept) {
		plog::Warn("affinity: cursor item %08x x%d sent to the client again: result %u%s", item.code, count,
		           static_cast<uint32_t>(sent), kept ? "" : ", ITEM CHANGED");
	}
}

// Game thread (#1744): a hand drop's 0x21, which D2R cannot resolve on a Maps or Bossing page. The cursor item is
// destroyed (or, past a full stack, cut to the rest) only by Merge's last step after the stack holds the sum, so a failure
// anywhere leaves it on the cursor. Every path past the cursor check ends in ResendCursor (#1762).
static_assert(sizeof(void*) == 8, "StackOnto packs both runtime ids into the callback's pointer");
void StackNow(const D2RL::PluginContext*, void* user) noexcept {
	const auto ids = reinterpret_cast<uintptr_t>(user);
	const uint32_t cursorId = static_cast<uint32_t>(ids >> 32);
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	D2RL::ItemHandle held {};
	items::ItemInfo item {.structSize = items::ItemInfoSize};
	Find stack {.id = static_cast<uint32_t>(ids)};
	if (g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success ||
	    g_inventory->getCursorItem(g_context, player, &held) != D2RL::Inventory::Result::Success ||
	    g_items->getItemInfo(g_context, held, &item) != items::Result::Success || item.runtimeId != cursorId ||
	    (g_inventory->forEachInventoryItem(g_context, player, &StashOnly, FindStored, &stack), stack.info.runtimeId != stack.id)) {
		plog::Info("affinity: hand stack of item %u onto %u: the cursor item or the stack is gone; nothing changed", cursorId, stack.id);
		if (player != D2RL::InvalidPlayerHandle) {
			ResendCursor(player, cursorId);  // a stack gone with the cursor item still held
		}
		return;
	}
	const items::ItemInfo& on = stack.info;
	const uint32_t page = g_viewPage.load();
	if (on.container != C::SharedStash || on.sharedStashPage != page || !aff::Stacks(item.code) ||
	    aff::Normal(item.code) != aff::Normal(on.code) ||
	    !aff::AtHome(aff::HomeOf(on.code), on.code, g_want.load(), true, on.sharedStashPage, on.x, on.y) || !StackAllowed(item, on)) {
		plog::Info("affinity: hand stack of %08x onto %08x (page %u cell %d,%d): not its own stack on page %u, or maps that do not stack; it stays on the cursor",
		           item.code, on.code, on.sharedStashPage, on.x, on.y, page);
	} else if (Merge(player, item, stack.info, true) == items::Result::Success) {
		++g_handStacked;
	}  // Merge logs a failure; the cursor item is kept then
	ResendCursor(player, cursorId);
}

// The pass's own copy of a routed item follows its move or merge, so a later item of the pass sees the new place (#1759)
void SyncReserved(const items::ItemInfo& item) noexcept {
	for (uint32_t i = 0; i < g_pass.reservedCount; ++i) {
		if (g_pass.reserved[i].runtimeId == item.runtimeId) {
			g_pass.reserved[i] = item;
		}
	}
}

// True when the item is home (or merged there); false when it went back to the inventory

}  // namespace detail

}  // namespace stash_affinity
