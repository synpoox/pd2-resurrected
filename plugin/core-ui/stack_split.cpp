// The split, all through the PluginSDK (no D2R hook): the SDK has no mouse event and no event for an item put down
// from the cursor (ItemInteractionService skips cursor paths), so a UI-thread watch sees the cursor each update. When a
// stack of N > 1 leaves it with Ctrl down, a game-thread job finds that same stack, whole, in a stored grid and
// splitStack takes N-1 of it back to the cursor. D2R itself chose the cell and the panel (inventory, cube, stash), and
// splitStack moves quantity between the stack and a new cursor stack in one step, so nothing is lost or made twice.
// PD2 instead sends its own packet 0x22 and makes a new 1-quantity item at the cell (docs/research/stack-split.md);
// the result is the same, but the one item left down here is the original and the cursor stack is the new one.
//
// shortcut: a TCP/IP client has no game thread (runOnGameThread is Unavailable), so it gets no split; upgrade with a
// plugin message to the host if online play needs it.
#include "stack_split.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/threads.h>

#include <atomic>
#include <cstdio>

namespace stack_split {

namespace {

namespace items = D2RL::Items;
using C = items::ItemContainer;

static_assert(SplitOff(5, true, 5, C::Inventory) == 4);
static_assert(SplitOff(2, true, 2, C::Cube) == 1);
static_assert(SplitOff(40, true, 40, C::SharedStash) == 39);
static_assert(SplitOff(5, false, 5, C::Inventory) == 0);   // plain click: the whole stack stays down
static_assert(SplitOff(1, true, 1, C::Inventory) == 0);    // one item: a normal put-down
static_assert(SplitOff(5, true, 8, C::Inventory) == 0);    // not the stack that left the cursor
static_assert(SplitOff(5, true, 5, C::Ground) == 0);       // dropped
static_assert(SplitOff(5, true, 5, C::Belt) == 0);

const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

// The cursor stack as last seen (UI thread only); quantity 0 when there is none
items::ItemInfo g_held {};

// One split in flight: the UI thread fills it, the game job reads it and clears `g_busy`
struct Job {
	D2RL::PlayerHandle player;
	uint32_t code;
	uint32_t runtimeId;
	int32_t held;
};
Job g_job {};
std::atomic<bool> g_busy {false};
bool g_watching = false;  // the UI-thread watch is queued (UI thread only)
std::atomic<uint32_t> g_splits {0}, g_failed {0};
uint32_t g_ticks = 0, g_putDowns = 0, g_ctrlPutDowns = 0;  // UI thread only; the status line reads them racily

auto FindPlaced(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* found = static_cast<items::ItemInfo*>(user);
	if (item->code != g_job.code || item->runtimeId != g_job.runtimeId) {
		return D2RL::Inventory::IterationAction::Continue;
	}
	*found = *item;
	return D2RL::Inventory::IterationAction::Stop;
}

void SplitNow(const D2RL::PluginContext*, void*) noexcept {
	const Job job = g_job;
	// The UI thread's handle is to the client's unit, stale on the game thread and without seeds, so find the server's
	// unit by its code and unit id, which the two share
	items::ItemInfo placed {};
	const D2RL::Inventory::ItemFilter filter {
		.structSize = D2RL::Inventory::ItemFilterSize,
		.containerMask = items::ContainerBit(C::Inventory) | items::ContainerBit(C::Cube) | items::ContainerBit(C::PersonalStash) |
		                 items::ContainerBit(C::SharedStash) | items::ContainerBit(C::CustomPage)};
	const auto listed = g_inventory->forEachInventoryItem(g_context, job.player, &filter, FindPlaced, &placed);
	g_busy = false;
	const uint32_t back = SplitOff(job.held, true, placed.quantity, placed.container);
	if (listed != D2RL::Inventory::Result::Success || back == 0) {
		// Not found: merged into another stack, or dropped; PD2 splits on an empty cell only
		plog::Info("split: none, list %u, %d held, %d found in container %u", static_cast<uint32_t>(listed), job.held,
		           placed.quantity, static_cast<uint32_t>(placed.container));
		return;
	}
	const items::SplitStackRequest request {.structSize = items::SplitStackRequestSize,
	                                        .player = job.player,
	                                        .sourceItem = placed.handle,
	                                        .quantity = back};
	items::SplitStackResult result {.structSize = items::SplitStackResultSize};
	const items::Result status = g_items->splitStack(g_context, &request, &result);
	if (status == items::Result::Success) {
		++g_splits;
	} else {
		++g_failed;
	}
	plog::Info("split: code %08x container %u cell %d,%d, %d held: result %u, %u left down, %u on the cursor",
	           placed.code, static_cast<uint32_t>(placed.container), placed.x, placed.y, job.held,
	           static_cast<uint32_t>(status), result.remainingQuantity, back);
}

void Watch(const D2RL::PluginContext*, void*) noexcept {
	g_watching = g_threads->runOnUiThread(g_context, Watch, nullptr) == D2RL::Threads::Result::Success;  // again next update
	++g_ticks;
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	D2RL::ItemHandle cursor = D2RL::InvalidItemHandle;
	if (g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success) {
		g_held.quantity = 0;
		return;
	}
	if (g_inventory->getCursorItem(g_context, player, &cursor) == D2RL::Inventory::Result::Success &&
	    cursor != D2RL::InvalidItemHandle) {
		g_held = {.structSize = items::ItemInfoSize};
		if (g_items->getItemInfo(g_context, cursor, &g_held) != items::Result::Success) {
			g_held.quantity = 0;
		}
		return;
	}
	const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	if (g_held.quantity > 1) {
		++g_putDowns;
		g_ctrlPutDowns += ctrl ? 1 : 0;
	}
	if (ctrl && g_held.quantity > 1 && !g_busy.exchange(true)) {
		g_job = {player, g_held.code, g_held.runtimeId, g_held.quantity};
		if (const auto queued = g_threads->runOnGameThread(g_context, SplitNow, nullptr); queued != D2RL::Threads::Result::Success) {
			g_busy = false;
			plog::Info("split: could not queue it (%u)", static_cast<uint32_t>(queued));
		}
	}
	g_held.quantity = 0;
}

// UI thread. The SDK refuses UI work queued during plugin load, so the watch starts with the first game.
void __cdecl StartWatch(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	if (!g_watching) {
		Watch(context, nullptr);
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
	if (!(Query(g_items, D2RL::HasItemServiceField, D2RL::ItemServiceSplitStackFieldEnd) &
	      Query(g_inventory, D2RL::HasInventoryServiceField, D2RL::InventoryServiceRequiredSize) &
	      Query(g_threads, D2RL::HasThreadServiceField, D2RL::ThreadServiceRequiredSize) &
	      Query(lifecycle, D2RL::HasLifecycleServiceField, D2RL::LifecycleServiceRequiredSize))) {
		std::snprintf(report, size, "FAILED (an item, inventory, thread or lifecycle service is missing)");
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
	std::snprintf(report, size, "Ctrl+left-click puts one item of a held stack down");
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "stack splits %u (failed %u); watch updates %u, stacks put down %u (%u with Ctrl)", g_splits.load(),
	              g_failed.load(), g_ticks, g_putDowns, g_ctrlPutDowns);
}

}  // namespace stack_split
