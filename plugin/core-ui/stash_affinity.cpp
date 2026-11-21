// Stash affinity (#1553), all through the PluginSDK (no D2R hook). D2R's own quick-move (Ctrl+click, or the pad's)
// puts an inventory item into the stash tab on show; then a game-thread watch moves a map, bossing or map-material item
// on to its reserved shared stash page and cell (common/stash_home.h): a map to its base's row on its tier's storage
// page, or onto the Maps page when the Maps tab shows its base; a map currency to its cell beside the cube, merged into
// the stack there; a bossing item to the Bossing page. The watch notes those items in the inventory each game update;
// one that is gone from the inventory and in a stash on the next update was quick-moved there (a hand move passes
// through the cursor, so it is never routed). A full or missing page moves the item back to the inventory.
// executeExistingItemTransaction moves the item itself, so its rolls, sockets and quantity stay.
//
// The Maps tab (stash_tabs.cpp) names the base it shows (ShowBase); the next update moves the other bases' maps off
// the Maps page to their storage rows and the shown base's maps from its storage page onto the Maps page's map cells.
//
// Why not ItemInteractionService: it raised no event for an inventory click on this loader (almanac.cpp), and it does
// not see the pad's quick-move. Why not a hook: the SDK's moves, edits and inventory reads cover it.
//
// shortcut: a TCP/IP client has no game thread (runOnGameThread is Unavailable), so routing and the base swap run in
// single player and on the host only; upgrade with a plugin message to the host if online play needs it.
// shortcut: no chat line when a page is full (the item just comes back to the inventory, and the log says why); add
// one with a string if players miss it.
// shortcut: a currency that would pass its cell's stack of MaxStack comes back whole instead of topping the stack up.
#include "stash_affinity.h"

#include "log.h"
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
constexpr int32_t MaxStack = 50;  // misc.txt maxstack of every MapMaterials code (tools/tests/test_stash_affinity.py)
constexpr uint32_t TidyEvery = 25;  // updates between tidies while a reserved tab is open

// The routed items in the inventory at the last update (game thread only). An inventory holds at most 10 x 8 items.
struct Noted {
	uint32_t runtimeId;
	uint32_t code;
};
constexpr uint32_t MaxNoted = 128;
struct NotedList {
	uint32_t count = 0;
	Noted item[MaxNoted];
};
NotedList g_noted;

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
std::atomic<bool> g_tidyFailed {false};
std::atomic<bool> g_hinted {false}, g_stopped {false};
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
		list->item[list->count++] = {item->runtimeId, item->code};
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

// The first free spare cell of the Bossing page; {-1, -1} when they are all used
auto FreeSpareCell() noexcept -> aff::Cell {
	for (int32_t i = 0; i < aff::SpareCells; ++i) {
		if (const aff::Cell c = aff::SpareCell(i); !Used(aff::BossingPage, c.x, c.y)) {
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

// Into the same currency's stack on its cell; Conflict when the sum passes a stack. editItem and destroyItem refuse a
// shared-stash item on this loader (Unsupported, Slot2), so both go to the inventory, the stack takes the sum there, the
// incoming item goes and the stack moves back onto its cell. A failed step puts things back; the sum is never doubled.
auto Merge(D2RL::PlayerHandle player, items::ItemInfo& item, items::ItemInfo& stack) noexcept -> items::Result {
	const int32_t have = stack.quantity, add = item.quantity;
	if (have < 1 || add < 1) {
		return items::Result::Unsupported;  // loader 1.3.1 reads 0 for a PD2 stack (Slot2): the edit would overwrite the count
	}
	if (have + add > MaxStack) {
		return items::Result::Conflict;
	}
	const items::ItemDestination bag {.structSize = items::ItemDestinationSize, .container = C::Inventory,
	                                  .placement = items::Placement::Automatic};
	const uint32_t page = stack.sharedStashPage;
	const int32_t x = stack.x, y = stack.y;
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
	items::ItemEdit edit {.structSize = items::ItemEditSize, .fields = items::EditFieldBit(items::EditField::Quantity),
	                      .quantity = static_cast<uint32_t>(have + add)};
	if (result == items::Result::Success) {
		step = "edit";
		result = g_items->editItem(g_context, player, stack.handle, &edit);
		if (result == items::Result::Success) {
			step = "destroy";
			result = g_items->destroyItem(g_context, player, item.handle, items::SocketedItemPolicy::RejectIfNotEmpty);
			if (result != items::Result::Success) {
				edit.quantity = static_cast<uint32_t>(have);  // undo, so nothing is made from nothing
				g_items->editItem(g_context, player, stack.handle, &edit);
			}
		}
	}
	const auto back = MoveTo(player, stack, page, x, y);  // MoveTo notes the cell used again
	if (result != items::Result::Success) {
		plog::Info("affinity: %08x merge failed at %s (result %u); the stack went back: result %u", item.code, step,
		           static_cast<uint32_t>(result), static_cast<uint32_t>(back));
		return result;  // Route sends the incoming item back to the inventory
	}
	item.code = 0;  // gone: no later step of this pass reads it
	stack.quantity = have + add;
	++g_merged;
	plog::Info("affinity: %08x x%d merged with the stack on page %u cell %d,%d (now %d; back on its cell: result %u)",
	           stack.code, add, page, x, y, have + add, static_cast<uint32_t>(back));
	return items::Result::Success;  // a failed move back leaves the whole stack in the inventory: nothing lost
}

void Route(D2RL::PlayerHandle player, items::ItemInfo item) noexcept {
	const aff::Home home = aff::HomeOf(item.code);
	if (aff::AtHome(home, item.code, g_shown, item.container == C::SharedStash, item.sharedStashPage, item.x, item.y)) {
		return;
	}
	items::Result moved = items::Result::Conflict;
	if (home.category == aff::Category::Maps) {
		const bool shown = g_shown != 0 && aff::Normal(item.code) == aff::Normal(g_shown);
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
			moved = Merge(player, item, g_pass.reserved[on]);
		}
		if (moved != items::Result::Success && home.category == aff::Category::Bossing) {
			if (const aff::Cell spare = FreeSpareCell(); spare.x >= 0) {
				moved = MoveTo(player, item, home.page, spare.x, spare.y);
			}
		}
	}
	if (moved == items::Result::Success) {
		++g_routed;
		return;
	}
	// The page or cell is full or missing (add_shared_tabs below aff::RequiredExtraTabs): back to the inventory
	const items::ItemDestination back {.structSize = items::ItemDestinationSize, .container = C::Inventory,
	                                   .placement = items::Placement::Automatic};
	const items::Result returned = Move(player, item.handle, back);
	(returned == items::Result::Success ? g_returned : g_failed)++;
	plog::Info("affinity: %08x could not go to page %u (result %u); back to the inventory: result %u", item.code, home.page,
	           static_cast<uint32_t>(moved), static_cast<uint32_t>(returned));
	if (moved != items::Result::Unsupported && !g_hinted.exchange(true)) {  // Unsupported: a merge the loader cannot do
		plog::Info("affinity: the reserved pages are shared tabs %u-%u; set [d2rcore.stash] add_shared_tabs to at least %u",
		           aff::FirstReservedPage, aff::LastReservedPage, aff::RequiredExtraTabs);
	}
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
			if (home.category == aff::Category::Bossing && aff::InSpare(item.x, item.y) && At(home.page, home.row, home.column) == -1) {
				items::ItemInfo copy = item;  // a spare one, its own cell now free: onto it
				if (MoveTo(player, copy, home.page, home.column, home.row) == items::Result::Success) {
					At(home.page, home.row, home.column) = -2;
				}
			}
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
	g_noted = now;
	const uint32_t want = g_want.load();
	const bool swap = want != g_shown, tidy = g_open.load() && update % TidyEvery == 0;
	if (g_pass.gone.count == 0 && !swap && !tidy) {
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

void SetTabOpen(bool open) noexcept {
	g_open.store(open);
}

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	g_context = context;
	const D2RL::LifecycleService* lifecycle = nullptr;
	// `&`, not `&&`: every service is queried, so the report never blames a missing one by mistake
	if (!(Query(g_items, D2RL::HasItemServiceField, D2RL::ItemServiceRequiredSize) &
	      Query(g_inventory, D2RL::HasInventoryServiceField, D2RL::InventoryServiceRequiredSize) &
	      Query(g_threads, D2RL::HasThreadServiceField, D2RL::ThreadServiceRequiredSize) &
	      Query(lifecycle, D2RL::HasLifecycleServiceField, D2RL::LifecycleServiceRequiredSize))) {
		std::snprintf(report, size, "FAILED (an item, inventory, thread or lifecycle service is missing)");
		return;
	}
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
	              "affinity: %u routed (%u merged), %u base swaps, %u sent back to the inventory, %u stuck in the stash; %u updates",
	              g_routed.load(), g_merged.load(), g_swapped.load(), g_returned.load(), g_failed.load(), g_updates.load());
}

}  // namespace stash_affinity
