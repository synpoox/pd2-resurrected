// Stash affinity (#1553), all through the PluginSDK (no D2R hook). D2R's own quick-move (Ctrl+click, or the pad's)
// puts an inventory item into the stash tab on show; then a game-thread watch moves a map, bossing or map-material item
// on to its reserved shared stash page (common/stash_home.h), a map to its base's row on its tier page. The watch
// notes those items in the inventory each game update; one that is gone from the inventory and in a stash on the next
// update was quick-moved there (a hand move passes through the cursor, so it is never routed). A full or missing page
// moves the item back to the inventory. executeExistingItemTransaction moves the item itself, so its rolls, sockets
// and quantity stay.
//
// Why not ItemInteractionService: it raised no event for an inventory click on this loader (almanac.cpp), and it does
// not see the pad's quick-move. Why not a hook: the SDK's moves and inventory reads cover it.
//
// shortcut: a TCP/IP client has no game thread (runOnGameThread is Unavailable), so routing runs in single player and
// on the host only; upgrade with a plugin message to the host if online play needs it.
// shortcut: no chat line when a page is full (the item just comes back to the inventory, and the log says why); add
// one with a string if players miss it.
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

// One stash pass: the noted items that left the inventory, as found in a stash, and the map rows' used columns
struct Left {
	NotedList gone;
	uint32_t foundCount = 0;
	items::ItemInfo found[MaxNoted];
	uint32_t rows[3][aff::PageWidth] {};  // tier page -> row -> a bit per used column
};
Left g_left;

std::atomic<uintptr_t> g_generation {0};
uintptr_t g_seen = 0;  // the watch generation g_noted belongs to (game thread only)
std::atomic<bool> g_hinted {false}, g_stopped {false};
std::atomic<uint32_t> g_routed {0}, g_returned {0}, g_failed {0}, g_updates {0};

auto NoteInventory(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* list = static_cast<NotedList*>(user);
	if (aff::HomeOf(item->code).category != aff::Category::None && list->count < MaxNoted) {
		list->item[list->count++] = {item->runtimeId, item->code};
	}
	return D2RL::Inventory::IterationAction::Continue;
}

auto ReadStash(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* left = static_cast<Left*>(user);
	const uint32_t tier = item->sharedStashPage - aff::FirstReservedPage;
	if (item->container == C::SharedStash && item->sharedStashPage != UINT32_MAX && tier < 3 && item->y >= 0 &&
	    item->y < aff::PageWidth && item->x >= 0 && item->x < aff::PageWidth) {
		left->rows[tier][item->y] |= 1U << item->x;  // maps are 1 x 1
	}
	for (uint32_t i = 0; i < left->gone.count; ++i) {
		if (left->gone.item[i].runtimeId == item->runtimeId && left->gone.item[i].code == item->code &&
		    left->foundCount < MaxNoted) {
			left->found[left->foundCount++] = *item;
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

void Route(D2RL::PlayerHandle player, const items::ItemInfo& item) noexcept {
	const aff::Home home = aff::HomeOf(item.code);
	if (aff::AtHome(home, item.container == C::SharedStash, item.sharedStashPage, item.y)) {
		return;
	}
	items::ItemDestination to {.structSize = items::ItemDestinationSize, .container = C::SharedStash,
	                           .placement = items::Placement::Automatic, .sharedStashPage = home.page};
	int32_t column = 0;
	if (home.row >= 0) {
		uint32_t& used = g_left.rows[home.page - aff::FirstReservedPage][home.row];
		column = aff::FreeColumn(used);
		to.placement = items::Placement::Exact;
		to.x = static_cast<uint32_t>(column);
		to.y = static_cast<uint32_t>(home.row);
		if (column >= 0) {
			used |= 1U << column;
		}
	}
	const items::Result moved = column >= 0 ? Move(player, item.handle, to) : items::Result::Conflict;
	if (moved == items::Result::Success) {
		++g_routed;
		plog::Info("affinity: %08x from container %u page %u to page %u cell %u,%u", item.code,
		           static_cast<uint32_t>(item.container), item.sharedStashPage, home.page, to.x, to.y);
		return;
	}
	// The page is full or missing (add_shared_tabs below aff::RequiredExtraTabs): back to the inventory
	const items::ItemDestination back {.structSize = items::ItemDestinationSize, .container = C::Inventory,
	                                   .placement = items::Placement::Automatic};
	const items::Result returned = Move(player, item.handle, back);
	(returned == items::Result::Success ? g_returned : g_failed)++;
	plog::Info("affinity: %08x could not go to page %u row %d (result %u%s); back to the inventory: result %u", item.code,
	           home.page, home.row, static_cast<uint32_t>(moved), column < 0 ? ", row full" : "",
	           static_cast<uint32_t>(returned));
	if (!g_hinted.exchange(true)) {
		plog::Info("affinity: the reserved pages are shared tabs %u-%u; set [d2rcore.stash] add_shared_tabs to at least %u",
		           aff::FirstReservedPage, aff::MapMaterialsPage, aff::RequiredExtraTabs);
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
	}
	++g_updates;
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success) {
		g_noted.count = 0;
		return;
	}
	NotedList now;
	if (g_inventory->forEachInventoryItem(g_context, player, &InventoryOnly, NoteInventory, &now) != D2RL::Inventory::Result::Success) {
		return;
	}
	g_left.gone.count = 0;
	for (uint32_t i = 0; i < g_noted.count; ++i) {
		bool kept = false;
		for (uint32_t j = 0; j < now.count && !kept; ++j) {
			kept = now.item[j].runtimeId == g_noted.item[i].runtimeId;
		}
		if (!kept) {
			g_left.gone.item[g_left.gone.count++] = g_noted.item[i];
		}
	}
	g_noted = now;
	if (g_left.gone.count == 0) {
		return;
	}
	g_left.foundCount = 0;
	for (auto& tier : g_left.rows) {
		for (uint32_t& row : tier) {
			row = 0;
		}
	}
	if (g_inventory->forEachInventoryItem(g_context, player, &StashOnly, ReadStash, &g_left) != D2RL::Inventory::Result::Success) {
		return;
	}
	for (uint32_t i = 0; i < g_left.foundCount; ++i) {
		Route(player, g_left.found[i]);
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
	              aff::FirstReservedPage, aff::MapMaterialsPage, aff::RequiredExtraTabs);
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "affinity: %u routed, %u sent back to the inventory, %u stuck in the stash; %u updates",
	              g_routed.load(), g_returned.load(), g_failed.load(), g_updates.load());
}

}  // namespace stash_affinity
