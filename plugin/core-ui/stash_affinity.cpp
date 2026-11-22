// Stash affinity (#1553), all through the PluginSDK (no D2R hook). D2R's own quick-move (Ctrl+click, or the pad's)
// puts an inventory item into the stash tab on show; then a game-thread watch moves a map, bossing or map-material item
// on to its reserved shared stash page and cell (common/stash_home.h): a map to its base's row on its tier's storage
// page, or onto the Maps page when the Maps tab shows its base; a map currency to its cell beside the cube, merged into
// the stack there; a bossing item to the Bossing page. The watch notes those items in the inventory each game update;
// one that is gone from the inventory and in a stash on the next update was quick-moved there (a hand move passes
// through the cursor, so it is never routed). A full or missing page moves the item back to the inventory.
// executeExistingItemTransaction moves the item itself, so its rolls, sockets and quantity stay. A homed stack onto its
// cell's stack merges up to MaxStack (#1744); the counts are read off the native items (Quantity below).
//
// The Maps tab (stash_tabs.cpp) names the base it shows (ShowBase); the next update moves the other bases' maps off
// the Maps page to their storage rows and the shown base's maps from its storage page onto the Maps page's map cells.
//
// Why not ItemInteractionService: it raised no event for an inventory click on this loader (almanac.cpp), and it does
// not see the pad's quick-move. Why not a hook: the SDK's moves, edits and inventory reads cover it; only the stack
// count is read natively, through the SDK's editNativeItem.
//
// shortcut: a TCP/IP client has no game thread (runOnGameThread is Unavailable), so routing and the base swap run in
// single player and on the host only; upgrade with a plugin message to the host if online play needs it.
// shortcut: no chat line when a page is full (the item just comes back to the inventory, and the log says why); add
// one with a string if players miss it.
// A quick-moved currency or boss part tops its cell's stack up to its cap, as a hand drop does (#1758, #1771); the rest
// comes back to the inventory. A map's stack still takes only a whole stack that fits (MapRefusal).
#include "stash_affinity.h"

#include "log.h"
#include "map_stack.h"
#include "pd2_engine.h"
#include "stash_home.h"

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/threads.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace stash_affinity {

namespace {

namespace items = D2RL::Items;
namespace aff = pd2::affinity;
using C = items::ItemContainer;

const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

constexpr uint32_t Stashes = items::ContainerBit(C::PersonalStash) | items::ContainerBit(C::SharedStash);
constexpr D2RL::Inventory::ItemFilter InventoryOnly {.structSize = D2RL::Inventory::ItemFilterSize,
                                                     .containerMask = items::ContainerBit(C::Inventory)};
constexpr D2RL::Inventory::ItemFilter StashOnly {.structSize = D2RL::Inventory::ItemFilterSize, .containerMask = Stashes};
constexpr uint32_t ReservedPages = aff::LastReservedPage + 1 - aff::FirstReservedPage;
constexpr uint32_t TidyEvery = 25;  // updates between tidies while a reserved tab is open

// The routed items in the inventory at the last update (game thread only). An inventory holds at most 10 x 8 items.
struct Noted {
	uint32_t runtimeId;
	uint32_t code;
	D2RL::ItemHandle handle;  // good for the update that noted it
};
constexpr uint32_t MaxNoted = 128;
struct NotedList {
	uint32_t count = 0;
	Noted item[MaxNoted];
};
NotedList g_noted;
NotedList g_kept;  // big stacks Withdraw could not send home: not tried again while they stay in the inventory (#1763)

// One stash pass (game thread only): the noted items that left the inventory, as found in a stash, and the reserved
// pages' items with the cells they use (items there are 1 x 1, but for the Bossing page's)
constexpr uint32_t MaxReserved = ReservedPages * aff::PageWidth * aff::PageWidth;
struct Pass {
	NotedList gone;
	uint32_t foundCount = 0;
	items::ItemInfo found[MaxNoted];
	uint32_t reservedCount = 0;
	items::ItemInfo reserved[MaxReserved];
	uint32_t used[ReservedPages][aff::PageWidth] {};  // reserved page -> row -> a bit per used column
	int32_t at[2][aff::PageWidth][aff::PageWidth] {};  // Maps, Bossing page: row, column -> index in reserved; -1 empty, -2 used
};
Pass g_pass;

std::atomic<uintptr_t> g_generation {0};
uintptr_t g_seen = 0;  // the watch generation g_noted belongs to (game thread only)
uint32_t g_shown = 0;  // the base whose maps are on the Maps page (game thread only)
std::atomic<uint32_t> g_want {0};
std::atomic<bool> g_open {false};
std::atomic<uint32_t> g_viewPage {0};
std::atomic<bool> g_tidyFailed {false};
std::atomic<bool> g_hinted {false}, g_stopped {false};
std::atomic<uint32_t> g_handStacked {0};
std::atomic<uint32_t> g_routed {0}, g_merged {0}, g_swapped {0}, g_returned {0}, g_failed {0}, g_updates {0};

auto InCell(int32_t x, int32_t y) noexcept -> bool {
	return x >= 0 && x < aff::PageWidth && y >= 0 && y < aff::PageWidth;
}

auto Reserved(const items::ItemInfo& item) noexcept -> bool {
	return item.container == C::SharedStash && item.sharedStashPage != UINT32_MAX &&
	       item.sharedStashPage >= aff::FirstReservedPage && item.sharedStashPage <= aff::LastReservedPage;
}

// A fixed-cell page's (Maps, Bossing) cell, in the pass's map of items
auto At(uint32_t page, int32_t row, int32_t column) noexcept -> int32_t& {
	return g_pass.at[page == aff::BossingPage ? 1 : 0][row][column];
}

auto Used(uint32_t page, int32_t x, int32_t y) noexcept -> bool {
	return (g_pass.used[page - aff::FirstReservedPage][y] & (1U << x)) != 0;
}

void Use(uint32_t page, int32_t x, int32_t y, bool used) noexcept {
	uint32_t& row = g_pass.used[page - aff::FirstReservedPage][y];
	row = used ? row | (1U << x) : row & ~(1U << x);
}

auto NoteInventory(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* list = static_cast<NotedList*>(user);
	if (aff::HomeOf(item->code).category != aff::Category::None && list->count < MaxNoted) {
		list->item[list->count++] = {item->runtimeId, item->code, item->handle};
	}
	return D2RL::Inventory::IterationAction::Continue;
}

auto ReadStash(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* pass = static_cast<Pass*>(user);
	if (Reserved(*item) && InCell(item->x, item->y) && pass->reservedCount < MaxReserved) {
		Use(item->sharedStashPage, item->x, item->y, true);
		if ((item->sharedStashPage == aff::MapsPage || item->sharedStashPage == aff::BossingPage) && InCell(item->x, item->y)) {
			At(item->sharedStashPage, item->y, item->x) = static_cast<int32_t>(pass->reservedCount);
		}
		pass->reserved[pass->reservedCount++] = *item;
	}
	for (uint32_t i = 0; i < pass->gone.count; ++i) {
		if (pass->gone.item[i].runtimeId == item->runtimeId && pass->gone.item[i].code == item->code &&
		    pass->foundCount < MaxNoted) {
			pass->found[pass->foundCount++] = *item;
			break;
		}
	}
	return D2RL::Inventory::IterationAction::Continue;
}

auto Move(D2RL::PlayerHandle player, D2RL::ItemHandle item, const items::ItemDestination& to) noexcept -> items::Result {
	items::ExistingItemOperation op {};
	op.structSize = items::ExistingItemOperationSize;
	op.kind = items::ExistingItemOperationKind::Move;
	op.item = item;
	op.move.destination = to;
	const items::ExistingItemTransaction transaction {
		.structSize = items::ExistingItemTransactionSize, .player = player, .operationCount = 1, .operations = &op};
	items::ExistingItemTransactionResult result {.structSize = items::ExistingItemTransactionResultSize};
	return g_items->executeExistingItemTransaction(g_context, &transaction, &result);
}

// To a shared page: a cell (x, y), or anywhere with x -1. The pass's cells and the item's place follow the move.
auto MoveTo(D2RL::PlayerHandle player, items::ItemInfo& item, uint32_t page, int32_t x, int32_t y) noexcept -> items::Result {
	items::ItemDestination to {.structSize = items::ItemDestinationSize, .container = C::SharedStash,
	                           .placement = items::Placement::Automatic, .sharedStashPage = page};
	if (x >= 0) {
		to.placement = items::Placement::Exact;
		to.x = static_cast<uint32_t>(x);
		to.y = static_cast<uint32_t>(y);
	}
	const items::Result moved = Move(player, item.handle, to);
	if (moved != items::Result::Success) {
		return moved;
	}
	if (Reserved(item) && InCell(item.x, item.y)) {
		Use(item.sharedStashPage, item.x, item.y, false);
		if (item.sharedStashPage == aff::MapsPage || item.sharedStashPage == aff::BossingPage) {
			At(item.sharedStashPage, item.y, item.x) = -1;  // free for an item later in this pass
		}
	}
	if (x >= 0) {
		Use(page, x, y, true);
	}
	plog::Info("affinity: %08x from container %u page %u to page %u cell %d,%d", item.code,
	           static_cast<uint32_t>(item.container), item.sharedStashPage, page, x, y);
	item.container = C::SharedStash;
	item.sharedStashPage = page;
	item.x = x;
	item.y = y;  // -1, -1 after a move anywhere: its cell is not known, so it is never freed this pass
	return moved;
}

// The first free map cell of the Maps page; {-1, -1} when they are all used
auto FreeMapCell() noexcept -> aff::Cell {
	for (int32_t i = 0; i < aff::MapViewCells; ++i) {
		if (const aff::Cell c = aff::MapView(i); !Used(aff::MapsPage, c.x, c.y)) {
			return c;
		}
	}
	return {};
}

// A map to its base's row on its tier's storage page, or anywhere on that page when the row is full
auto ToStorage(D2RL::PlayerHandle player, items::ItemInfo& item) noexcept -> items::Result {
	const aff::Home home = aff::HomeOf(item.code);
	const int32_t column = aff::FreeColumn(g_pass.used[home.page - aff::FirstReservedPage][home.row]);
	return MoveTo(player, item, home.page, column, column >= 0 ? home.row : -1);
}

// An item's count: stat 70, read off the native item. Loader 1.3.1's ItemInfo.quantity reads 0 for PD2's map currency
// and boss parts (#1628: a stack the game draws as 2 read q0), and no SDK call merges two stacks (item.h: Debit, Edit,
// Move, splitStack), so Merge sets the sum with editItem. 0 when the read fails.
void ReadQuantity(const D2RL::PluginContext*, void* native, void* user) noexcept {
	*static_cast<int32_t*>(user) = d2r::Engine<int32_t (*)(d2r::Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(
	    static_cast<d2r::Unit*>(native), d2r::rva::StatQuantity, 0);
}

auto Quantity(D2RL::ItemHandle item) noexcept -> int32_t {
	int32_t count = 0;
	return g_items->editNativeItem(g_context, item, ReadQuantity, &count) == items::Result::Success ? count : 0;
}

void WriteQuantity(const D2RL::PluginContext*, void* native, void* user) noexcept {
	d2r::Engine<void (*)(d2r::Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::SetUnitStat)(
	    static_cast<d2r::Unit*>(native), d2r::rva::StatQuantity, *static_cast<int32_t*>(user), 0);
}

// Sets a stack's count, verified by the stat. editItem refuses a count past the item's maxstack (Slot5, #1763: result 2
// for 50 + 50), and no SDK call sets more, so a stash stack past 50 gets stat 70 natively (SetUnitStat, by signature)
// through editNativeItem; the caller's next move of the item publishes it to the client.
auto SetQuantity(D2RL::PlayerHandle player, D2RL::ItemHandle item, int32_t count) noexcept -> items::Result {
	const items::ItemEdit edit {.structSize = items::ItemEditSize, .fields = items::EditFieldBit(items::EditField::Quantity),
	                            .quantity = static_cast<uint32_t>(count)};
	if (g_items->editItem(g_context, player, item, &edit) == items::Result::Success && Quantity(item) == count) {
		return items::Result::Success;
	}
	if (d2r::sig::SetUnitStat.rva == 0 || g_items->editNativeItem(g_context, item, WriteQuantity, &count) != items::Result::Success) {
		return items::Result::Unsupported;
	}
	return Quantity(item) == count ? items::Result::Success : items::Result::Unsupported;
}

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
	plog::Info("affinity: map %08x (quality %u) onto the stack at page %u cell %d,%d (quality %u): stats equal %d, %s%s", a.code,
	           static_cast<uint32_t>(a.quality), b.sharedStashPage, b.x, b.y, static_cast<uint32_t>(b.quality), same,
	           reason == nullptr ? "merge" : "refused: ", reason == nullptr ? "" : reason);
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
auto Merge(D2RL::PlayerHandle player, items::ItemInfo& item, items::ItemInfo& stack, bool topUp = false) noexcept -> items::Result {
	const items::ItemDestination bag {.structSize = items::ItemDestinationSize, .container = C::Inventory,
	                                  .placement = items::Placement::Automatic};
	const uint32_t page = stack.sharedStashPage;
	const int32_t x = stack.x, y = stack.y;
	const int32_t MaxStack = aff::StackCap(stack.code);  // 255, or 50 for a map (#1763)
	// an unread count (0) is left to the read after the move below
	if (const int32_t was = Quantity(stack.handle), more = Quantity(item.handle);
	    was > 0 && more > 0 && (topUp ? aff::ToppedUp(was, more, MaxStack).stack : aff::MergedCount(was, more, MaxStack)) == 0) {
		plog::Info("affinity: %08x onto the stack on page %u cell %d,%d (%d + %d): it does not fit; nothing moved", item.code, page,
		           x, y, was, more);
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
		plog::Info("affinity: %08x merge failed at %s (result %u, stack %d + %d); the stack went back: result %u", item.code,
		           step, static_cast<uint32_t>(result), have, add, static_cast<uint32_t>(back));
		return result;  // Route sends the incoming item back to the inventory
	}
	if (rest == 0) {
		item.code = 0;  // gone: no later step of this pass reads it
	}
	stack.quantity = sum;
	++g_merged;
	plog::Info("affinity: %08x x%d merged with the stack on page %u cell %d,%d (now %d, %d left on the incoming item; back on its cell: result %u)",
	           stack.code, add - rest, page, x, y, sum, rest, static_cast<uint32_t>(back));
	return items::Result::Success;  // a failed move back leaves the whole stack in the inventory: nothing lost
}

struct Find {
	uint32_t id;
	items::ItemInfo info {};
};
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
	(sent == items::Result::Success && kept ? plog::Info : plog::Warn)(
	    "affinity: cursor item %08x x%d sent to the client again: result %u%s", item.code, count, static_cast<uint32_t>(sent),
	    kept ? "" : ", ITEM CHANGED");
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
// A fixed cell's stack tops up to its cap and the rest stays on the item, in the inventory (Withdraw #1763, quick-move #1771)
auto Route(D2RL::PlayerHandle player, items::ItemInfo item) noexcept -> bool {
	const aff::Home home = aff::HomeOf(item.code);
	if (aff::AtHome(home, item.code, g_shown, item.container == C::SharedStash, item.sharedStashPage, item.x, item.y)) {
		return true;
	}
	items::Result moved = items::Result::Conflict;
	if (home.category == aff::Category::Maps) {
		const bool shown = g_shown != 0 && aff::Normal(item.code) == aff::Normal(g_shown);
		// A stack of the same map below a full one takes it first (#1759); a full stack or a failed merge falls through
		const uint32_t stackPage = shown ? aff::MapsPage : home.page;
		for (uint32_t i = 0; i < g_pass.reservedCount && moved != items::Result::Success; ++i) {
			items::ItemInfo& stack = g_pass.reserved[i];
			if (stack.code != 0 && stack.runtimeId != item.runtimeId && stack.container == C::SharedStash &&
			    aff::Normal(stack.code) == aff::Normal(item.code) &&
			    MapRefusal(item, stack, stackPage, aff::InMapView(stack.x, stack.y), shown) == nullptr) {
				moved = Merge(player, item, stack);
			}
		}
		if (moved == items::Result::Success) {
			SyncReserved(item);
			++g_routed;
			return true;
		}
		const aff::Cell cell = shown ? FreeMapCell() : aff::Cell {};
		moved = cell.x >= 0 ? MoveTo(player, item, aff::MapsPage, cell.x, cell.y) : ToStorage(player, item);
	} else {  // a fixed cell: Map Materials, Bossing
		int32_t& on = At(home.page, home.row, home.column);
		if (on == -1) {
			moved = MoveTo(player, item, home.page, home.column, home.row);
			if (moved == items::Result::Success) {
				on = -2;  // used by an item this pass did not read
			}
		} else if (on >= 0 && aff::Stacks(item.code) && aff::Normal(g_pass.reserved[on].code) == aff::Normal(item.code)) {
			moved = Merge(player, item, g_pass.reserved[on], true);
		}
	}
	if (moved == items::Result::Success) {
		SyncReserved(item);
		++g_routed;
		return true;
	}
	// The page or cell is full or missing (add_shared_tabs below aff::RequiredExtraTabs): back to the inventory
	const items::ItemDestination back {.structSize = items::ItemDestinationSize, .container = C::Inventory,
	                                   .placement = items::Placement::Automatic};
	const items::Result returned = Move(player, item.handle, back);
	(returned == items::Result::Success ? g_returned : g_failed)++;
	plog::Info("affinity: %08x could not go to page %u (result %u); back to the inventory: result %u", item.code, home.page,
	           static_cast<uint32_t>(moved), static_cast<uint32_t>(returned));
	if (moved != items::Result::Unsupported && !g_hinted.exchange(true)) {  // Unsupported: a stack count that could not be read
		plog::Info("affinity: the reserved pages are shared tabs %u-%u; set [d2rcore.stash] add_shared_tabs to at least %u",
		           aff::FirstReservedPage, aff::LastReservedPage, aff::RequiredExtraTabs);
	}
	return false;
}

// A stack past aff::WithdrawMax new in the inventory (#1763: Ctrl+click out of the stash, or a hand pick-up put down):
// WithdrawMax of it split onto the cursor (the SDK moves no cursor item, so it stays there), the rest back home through
// Route, topping the cell's stack up. A busy cursor sends the whole stack home; a failed split keeps it in the inventory
// (it could never be taken out otherwise) and logs the result. Only counts move between the two items, checked by reading them back.
void Withdraw(D2RL::PlayerHandle player, const Noted& noted) noexcept {
	items::ItemInfo item {.structSize = items::ItemInfoSize};
	if (g_items->getItemInfo(g_context, noted.handle, &item) != items::Result::Success) {
		return;
	}
	const int32_t had = Quantity(item.handle);
	D2RL::ItemHandle held = D2RL::InvalidItemHandle;
	items::Result split = items::Result::Conflict;
	bool busy = true, sums = true;
	if (g_inventory->getCursorItem(g_context, player, &held) == D2RL::Inventory::Result::NotFound) {  // NotFound: an empty cursor
		const items::SplitStackRequest request1 {.structSize = items::SplitStackRequestSize, .player = player,
		                                         .sourceItem = item.handle, .quantity = 1};
		items::SplitStackResult result {.structSize = items::SplitStackResultSize};
		busy = false;
		// splitStack refuses a source past its maxstack (Unavailable, Slot5): split 1 off a stack of 2, then set both counts.
		// The source's rest is past 50 and only the native write sets it: without SetUnitStat nothing is touched. Every
		// failure puts the counts back from what is read, never assumed, and keeps the source in the inventory.
		D2RL::ItemHandle piece = D2RL::InvalidItemHandle;
		if (d2r::sig::SetUnitStat.rva == 0 || had <= aff::WithdrawMax) {
			split = items::Result::Unsupported;
		} else if ((split = SetQuantity(player, item.handle, 2)) != items::Result::Success) {
			if (SetQuantity(player, item.handle, had) != items::Result::Success) {
				plog::Warn("affinity: %08x x%d: set to 2 failed and the count could not be put back", item.code, had);
			}
		} else if ((split = g_items->splitStack(g_context, &request1, &result)) != items::Result::Success) {
			if (SetQuantity(player, item.handle, had) != items::Result::Success) {
				plog::Warn("affinity: %08x x%d: split failed and the count could not be put back", item.code, had);
			}
		} else {
			piece = result.cursorItem;  // source 1, piece 1
			if ((split = SetQuantity(player, item.handle, had - aff::WithdrawMax)) != items::Result::Success) {
				if (SetQuantity(player, item.handle, had - 1) != items::Result::Success) {
					plog::Warn("affinity: %08x x%d: source write failed, piece 1, source %d", item.code, had, Quantity(item.handle));
				}
			} else if ((split = SetQuantity(player, piece, aff::WithdrawMax)) != items::Result::Success) {
				int32_t got = Quantity(piece);  // 0: unreadable
				if (got <= 0) {
					const items::ItemEdit one {.structSize = items::ItemEditSize,
					                           .fields = items::EditFieldBit(items::EditField::Quantity), .quantity = 1};
					got = g_items->editItem(g_context, player, piece, &one) == items::Result::Success ? Quantity(piece) : 0;
				}
				if (got <= 0 || SetQuantity(player, item.handle, had - got) != items::Result::Success) {
					plog::Warn("affinity: %08x x%d: piece write failed, piece %d, source %d", item.code, had, got, Quantity(item.handle));
				}
			}
		}
		// self-check, read back: the two items hold what the stack had
		const int32_t source = Quantity(item.handle), cut = piece != D2RL::InvalidItemHandle ? Quantity(piece) : 0;
		sums = source + cut == had;
		if (!sums) {
			plog::Error("affinity: %08x withdrawal does not add up: had %d, source %d + piece %d", item.code, had, source, cut);
			split = split == items::Result::Success ? items::Result::Conflict : split;
		}
	}
	// An item with no home (Uber Ancients keys, #1785) has nowhere to route to: the rest stays in the inventory
	// A busy cursor, or a second homeless stack in the same pass, stays whole and is not retried. The guard also keeps
	// Route off At(0, -1, -1), out of bounds for a homeless code.
	const bool homeless = aff::HomeOf(item.code).category == aff::Category::None;
	const bool home = (split == items::Result::Success || busy) && !homeless && Route(player, item);
	if (homeless && split == items::Result::Success) {  // publishes the rest's native count (SetQuantity); the client still draws 1
		const items::ItemDestination back {.structSize = items::ItemDestinationSize, .container = C::Inventory,
		                                   .placement = items::Placement::Automatic};
		Move(player, item.handle, back);
	}
	// #1772: the source's move home made it the client's cursor item and put it down (ResendCursor), so the piece (or a
	// busy cursor's item) was held but not drawn; send the cursor item again after that last move
	items::ItemInfo cursor {.structSize = items::ItemInfoSize};
	if (g_inventory->getCursorItem(g_context, player, &held) == D2RL::Inventory::Result::Success &&
	    g_items->getItemInfo(g_context, held, &cursor) == items::Result::Success) {
		ResendCursor(player, cursor.runtimeId);
	}
	plog::Info("affinity: %08x x%d taken out of the stash: %d to the cursor (split result %u, counts %s), the rest %s", item.code,
	           had, split == items::Result::Success ? aff::WithdrawMax : 0, static_cast<uint32_t>(split),
	           busy ? "untouched" : sums ? "add up" : "DO NOT add up", home ? "back home" : "kept in the inventory");
	if (!home && g_kept.count < MaxNoted) {
		g_kept.item[g_kept.count++] = noted;
	}
}

auto Listed(const NotedList& list, uint32_t runtimeId) noexcept -> bool {
	for (uint32_t i = 0; i < list.count; ++i) {
		if (list.item[i].runtimeId == runtimeId) {
			return true;
		}
	}
	return false;
}

// The other bases' maps off the Maps page, then the wanted base's maps onto it
void Swap(D2RL::PlayerHandle player, uint32_t want) noexcept {
	uint32_t off = 0, on = 0;
	for (uint32_t i = 0; i < g_pass.reservedCount; ++i) {
		items::ItemInfo& item = g_pass.reserved[i];
		if (item.sharedStashPage == aff::MapsPage && aff::InMapView(item.x, item.y) && aff::BaseOf(item.code).tier >= 0 &&
		    aff::Normal(item.code) != aff::Normal(want) && ToStorage(player, item) == items::Result::Success) {
			++off;
		}
	}
	if (const aff::Base base = aff::BaseOf(want); base.tier >= 0) {
		const uint32_t page = aff::FirstStoragePage + static_cast<uint32_t>(base.tier);
		for (uint32_t i = 0; i < g_pass.reservedCount; ++i) {
			items::ItemInfo& item = g_pass.reserved[i];
			if (item.sharedStashPage != page || aff::Normal(item.code) != aff::Normal(want)) {
				continue;
			}
			const aff::Cell cell = FreeMapCell();
			if (cell.x < 0) {
				break;  // the rest stay stored
			}
			if (MoveTo(player, item, aff::MapsPage, cell.x, cell.y) == items::Result::Success) {
				++on;
			}
		}
	}
	g_shown = want;
	++g_swapped;
	plog::Info("affinity: the Maps tab shows %08x: %u maps stored, %u shown", want, off, on);
}

// While a reserved tab is up: an item on the Maps or Bossing page off its place (one put straight onto the page on show,
// or left under the buttons or the cube) goes where Route sends it; an item no rule takes goes back to the inventory
void Tidy(D2RL::PlayerHandle player) noexcept {
	for (uint32_t i = 0; i < g_pass.reservedCount; ++i) {
		const items::ItemInfo& item = g_pass.reserved[i];
		if (item.code == 0 || (item.sharedStashPage != aff::MapsPage && item.sharedStashPage != aff::BossingPage) ||
		    !InCell(item.x, item.y)) {
			continue;
		}
		const aff::Home home = aff::HomeOf(item.code);
		if (aff::AtHome(home, item.code, g_shown, true, item.sharedStashPage, item.x, item.y)) {
			continue;
		}
		bool found = false;  // Route has it already, from its own copy
		for (uint32_t j = 0; j < g_pass.foundCount && !found; ++j) {
			found = g_pass.found[j].runtimeId == item.runtimeId;
		}
		if (found) {
			continue;
		}
		if (home.category != aff::Category::None) {
			Route(player, item);
			continue;
		}
		const items::ItemDestination back {.structSize = items::ItemDestinationSize, .container = C::Inventory,
		                                   .placement = items::Placement::Automatic};
		const items::Result moved = Move(player, item.handle, back);
		if (moved == items::Result::Success || !g_tidyFailed.exchange(true)) {  // a full inventory logs once, not every pass
			plog::Info("affinity: %08x on page %u at %d,%d belongs to no reserved tab: back to the inventory (result %u)",
			           item.code, item.sharedStashPage, item.x, item.y, static_cast<uint32_t>(moved));
		}
		// a stack past the inventory cap is split now, not left whole (#1785)
		if (moved == items::Result::Success && aff::Stacks(item.code) && Quantity(item.handle) > aff::WithdrawMax) {
			Withdraw(player, Noted {item.runtimeId, item.code, item.handle});
		}
	}
}

void Update(const D2RL::PluginContext*, void* user) noexcept {
	const auto generation = reinterpret_cast<uintptr_t>(user);
	if (generation != g_generation.load()) {
		return;  // a newer game started its own watch
	}
	if (const auto queued = g_threads->runOnGameThread(g_context, Update, user); queued != D2RL::Threads::Result::Success) {
		if (!g_stopped.exchange(true)) {
			plog::Info("affinity: the watch stopped (%u) until the next game", static_cast<uint32_t>(queued));
		}
		return;
	}
	if (generation != g_seen) {  // a new game: runtime ids from the last one mean nothing here
		g_seen = generation;
		g_noted.count = 0;
		g_kept.count = 0;
		g_shown = 0;
	}
	const uint32_t update = ++g_updates;
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success) {
		g_noted.count = 0;
		return;
	}
	NotedList now;
	if (g_inventory->forEachInventoryItem(g_context, player, &InventoryOnly, NoteInventory, &now) != D2RL::Inventory::Result::Success) {
		return;
	}
	g_pass.gone.count = 0;
	for (uint32_t i = 0; i < g_noted.count; ++i) {
		bool kept = false;
		for (uint32_t j = 0; j < now.count && !kept; ++j) {
			kept = now.item[j].runtimeId == g_noted.item[i].runtimeId;
		}
		if (!kept) {
			g_pass.gone.item[g_pass.gone.count++] = g_noted.item[i];
		}
	}
	// stacks past WithdrawMax new in the inventory (#1763); kept ones that left it may be tried again later
	// with the stash closed they wait unnoted: a 50 on the cursor out of a panel was dropped by the next click (Slot5)
	NotedList big;
	NotedList kept;
	NotedList noted;
	const bool open = g_open.load();
	for (uint32_t j = 0; j < now.count; ++j) {
		const Noted& n = now.item[j];
		if (Listed(g_kept, n.runtimeId)) {
			kept.item[kept.count++] = n;
		} else if (!Listed(g_noted, n.runtimeId) && aff::StackCap(n.code) > aff::WithdrawMax && aff::Stacks(n.code) &&
		           Quantity(n.handle) > aff::WithdrawMax) {
			if (!open) {
				continue;
			}
			big.item[big.count++] = n;
		}
		noted.item[noted.count++] = n;
	}
	g_kept = kept;
	g_noted = noted;
	const uint32_t want = g_want.load();
	const bool swap = want != g_shown, tidy = g_open.load() && update % TidyEvery == 0;
	if (g_pass.gone.count == 0 && !swap && !tidy && big.count == 0) {
		return;
	}
	g_pass.foundCount = 0;
	g_pass.reservedCount = 0;
	for (auto& page : g_pass.used) {
		for (uint32_t& row : page) {
			row = 0;
		}
	}
	for (auto& page : g_pass.at) {
		for (auto& row : page) {
			for (int32_t& cell : row) {
				cell = -1;
			}
		}
	}
	if (g_inventory->forEachInventoryItem(g_context, player, &StashOnly, ReadStash, &g_pass) != D2RL::Inventory::Result::Success) {
		return;
	}
	if (swap) {
		Swap(player, want);
	}
	for (uint32_t i = 0; i < g_pass.foundCount; ++i) {
		Route(player, g_pass.found[i]);
	}
	for (uint32_t i = 0; i < big.count; ++i) {
		Withdraw(player, big.item[i]);
	}
	if (tidy) {
		Tidy(player);
	}
}

void __cdecl StartWatch(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	const uintptr_t generation = ++g_generation;
	g_stopped = false;
	if (const auto queued = g_threads->runOnGameThread(context, Update, reinterpret_cast<void*>(generation));
	    queued != D2RL::Threads::Result::Success) {
		plog::Info("affinity: no game thread (%u): routing runs in single player and on the host only", static_cast<uint32_t>(queued));
	}
}

template <class Service>
auto Query(const Service*& service, bool (*has)(const Service*, uint32_t), uint32_t required) noexcept -> bool {
	if (g_context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !has(service, required)) {
		service = nullptr;
	}
	return service != nullptr;
}

}  // namespace

void ShowBase(uint32_t code) noexcept {
	g_want.store(code);
}

void SetTabOpen(bool open, uint32_t page) noexcept {
	g_open.store(open);
	g_viewPage.store(open ? page : 0);
}

auto ViewPage() noexcept -> uint32_t {
	return g_viewPage.load();
}

auto StackOnto(uint32_t cursorId, uint32_t targetId) noexcept -> bool {
	if (g_threads == nullptr || g_inventory == nullptr || g_items == nullptr) {
		return false;
	}
	void* ids = reinterpret_cast<void*>(static_cast<uintptr_t>(cursorId) << 32 | targetId);
	return g_threads->runOnGameThread(g_context, StackNow, ids) == D2RL::Threads::Result::Success;
}

auto Resend(uint32_t itemId) noexcept -> bool {
	if (g_threads == nullptr || g_inventory == nullptr || g_items == nullptr) {
		return false;
	}
	const auto now = [](const D2RL::PluginContext*, void* user) noexcept {
		D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
		if (g_inventory->getLocalPlayer(g_context, &player) == D2RL::Inventory::Result::Success) {
			ResendCursor(player, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(user)));
		}
	};
	return g_threads->runOnGameThread(g_context, now, reinterpret_cast<void*>(static_cast<uintptr_t>(itemId))) ==
	       D2RL::Threads::Result::Success;
}

auto TakeOut(uint32_t itemId) noexcept -> bool {
	if (g_threads == nullptr || g_inventory == nullptr || g_items == nullptr) {
		return false;
	}
	const auto now = [](const D2RL::PluginContext*, void* user) noexcept {
		D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
		Find stack {.id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(user))};
		if (g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success ||
		    (g_inventory->forEachInventoryItem(g_context, player, &StashOnly, FindStored, &stack), stack.info.runtimeId != stack.id)) {
			plog::Info("affinity: take of item %u: it is gone from the stash; nothing moved", stack.id);
			return;
		}
		// shortcut: a full inventory leaves the stack in its cell (logged); upgrade with a chat line if players miss it
		const items::ItemDestination bag {.structSize = items::ItemDestinationSize, .container = C::Inventory,
		                                  .placement = items::Placement::Automatic};
		const items::ItemInfo& on = stack.info;
		const auto moved = Move(player, on.handle, bag);
		plog::Info("affinity: %08x x%d taken from page %u cell %d,%d to the inventory for the split: result %u", on.code,
		           Quantity(on.handle), on.sharedStashPage, on.x, on.y, static_cast<uint32_t>(moved));
		if (moved != items::Result::Success) {
			// the consumed take left the stack pending on the client (0x1c6ac0) until an item packet: a move onto its own
			// cell sends one, else anywhere on its page (the tidy pass routes it home)
			items::ItemDestination home {.structSize = items::ItemDestinationSize, .container = C::SharedStash,
			                             .placement = items::Placement::Exact, .sharedStashPage = on.sharedStashPage};
			home.x = static_cast<uint32_t>(on.x);
			home.y = static_cast<uint32_t>(on.y);
			auto resent = Move(player, on.handle, home);
			if (resent != items::Result::Success) {
				home.placement = items::Placement::Automatic;
				resent = Move(player, on.handle, home);
			}
			(resent == items::Result::Success ? plog::Info : plog::Warn)(
			    "affinity: %08x stays in the stash; sent to the client again by a move on page %u: result %u", on.code,
			    on.sharedStashPage, static_cast<uint32_t>(resent));
		}
	};
	return g_threads->runOnGameThread(g_context, now, reinterpret_cast<void*>(static_cast<uintptr_t>(itemId))) ==
	       D2RL::Threads::Result::Success;
}

auto WantedBase() noexcept -> uint32_t {
	return g_want.load();
}

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	g_context = context;
	const D2RL::LifecycleService* lifecycle = nullptr;
	// `&`, not `&&`: every service is queried, so the report never blames a missing one by mistake
	if (!(Query(g_items, D2RL::HasItemServiceField, D2RL::ItemServiceSplitStackFieldEnd) &  // Withdraw splits (#1763)
	      Query(g_inventory, D2RL::HasInventoryServiceField, D2RL::InventoryServiceRequiredSize) &
	      Query(g_threads, D2RL::HasThreadServiceField, D2RL::ThreadServiceRequiredSize) &
	      Query(lifecycle, D2RL::HasLifecycleServiceField, D2RL::LifecycleServiceRequiredSize))) {
		std::snprintf(report, size, "FAILED (an item, inventory, thread or lifecycle service is missing)");
		return;
	}
	sigscan::Entry* const sigs[] = {&d2r::sig::SetUnitStat};  // SetQuantity past 50 (#1763); unresolved: stacks stop at 50
	sigscan::Resolve(sigs, 1, context->exeBase, nullptr, 0);
	if (!D2RL::HasItemServiceCapability(g_items, items::ItemServiceCapability::SharedStashWrite)) {
		std::snprintf(report, size, "FAILED (the item service cannot write the shared stash)");
		return;
	}
	const D2RL::Lifecycle::GameplayEventListener listener {.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
	                                                       .kind = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
	                                                       .callback = StartWatch};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	if (lifecycle->registerGameplayEventListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
		std::snprintf(report, size, "FAILED (no player-ready listener)");
		return;
	}
	std::snprintf(report, size, "quick-moved maps, bossing and map materials go to shared tabs %u-%u (add_shared_tabs >= %u)",
	              aff::FirstReservedPage, aff::LastReservedPage, aff::RequiredExtraTabs);
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size,
	              "affinity: %u routed (%u merged, %u hand stacks), %u base swaps, %u sent back to the inventory, %u stuck in the stash; %u updates",
	              g_routed.load(), g_merged.load(), g_handStacked.load(), g_swapped.load(), g_returned.load(), g_failed.load(), g_updates.load());
}

}  // namespace stash_affinity
