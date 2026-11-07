// The split, all through the PluginSDK (no D2R hook): the SDK has no mouse event and no event for an item put down
// from the cursor (ItemInteractionService skips cursor paths), so a UI-thread watch sees the cursor each update. When a
// stack of N > 1 leaves it with Ctrl down, a game-thread job finds that same stack, whole, in a stored grid and
// splitStack takes N-1 of it back to the cursor. D2R itself chose the cell and the panel (inventory, cube, stash), and
// splitStack moves quantity between the stack and a new cursor stack in one step, so nothing is lost or made twice.
// PD2 instead sends its own packet 0x22 and makes a new 1-quantity item at the cell (docs/research/stack-split.md);
// the result is the same, but the one item left down here is the original and the cursor stack is the new one.
// In core-ui (client-only, ADR 0004) though it changes items: the result is what a put-down and a pick-up would give.
//
// shortcut: a TCP/IP client has no game thread (runOnGameThread is Unavailable), so it gets no split; upgrade with a
// plugin message to the host if online play needs it.
// A Ctrl+click on a stored stack of the same item (#362): D2R merges the whole held stack into it, so while a stack is
// held the watch notes the stored stacks with its code, and the job splits N-1 back off the one that rose by N.
// shortcut: a merge that stops at the stack cap leaves the rest on the cursor, and that is kept as D2R does it (PD2
// would add one); upgrade if players miss it.
// shortcut: the job runs on the next game update and looks once; a server that has not placed the item by then skips
// the split (logged), fine on a local game, upgrade with a retry if hosted games miss splits.
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

constexpr uint32_t El = items::MakeItemCode("r01");
static_assert(SplitOff(El, 5, true, 0, 5, C::Inventory) == 4);
static_assert(SplitOff(El, 2, true, 0, 2, C::Cube) == 1);
static_assert(SplitOff(El, 40, true, 0, 40, C::SharedStash) == 39);
static_assert(SplitOff(El, 5, false, 0, 5, C::Inventory) == 0);   // plain click: the whole stack stays down
static_assert(SplitOff(El, 1, true, 0, 1, C::Inventory) == 0);    // one item: a normal put-down
static_assert(SplitOff(El, 5, true, 0, 8, C::Inventory) == 0);    // not the stack that left the cursor
static_assert(SplitOff(El, 5, true, 0, 5, C::Ground) == 0);       // dropped
static_assert(SplitOff(El, 5, true, 0, 5, C::Belt) == 0);
static_assert(SplitOff(items::MakeItemCode("tbk"), 20, true, 0, 20, C::Inventory) == 0);  // tome of town portal
static_assert(SplitOff(El, 5, true, 2, 7, C::Inventory) == 4);    // merged into a stack of 2: it keeps 3
static_assert(SplitOff(El, 5, false, 2, 7, C::Inventory) == 0);   // plain click merges the whole stack
static_assert(SplitOff(El, 5, true, 2, 6, C::Inventory) == 0);    // rose by less than the held stack

const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

// The cursor stack as last seen (UI thread only); quantity 0 when there is none
items::ItemInfo g_held {};

// A stored stack with the held item's code, as it was when that stack was picked up
struct Stack {
	uint32_t runtimeId;
	int32_t quantity;
};
constexpr uint32_t MaxStacks = 64;  // shortcut: stacks past 64 of one code are not noted (no merge split onto them)

struct Stacks {
	uint32_t code;
	uint32_t count;
	Stack stack[MaxStacks];
};

// One split in flight: the UI thread fills it, the game job reads it and clears `g_busy`
struct Job {
	D2RL::PlayerHandle player;
	uint32_t code;
	uint32_t runtimeId;
	int32_t held;
	Stacks stored;
};
Job g_job {};
Stacks g_stored {};  // UI thread only: noted for g_held
std::atomic<bool> g_busy {false};
bool g_watching = false;  // the UI-thread watch is queued (UI thread only)
std::atomic<uint32_t> g_splits {0}, g_failed {0};
uint32_t g_ticks = 0, g_putDowns = 0, g_ctrlPutDowns = 0;  // UI thread only; the status line reads them racily

constexpr D2RL::Inventory::ItemFilter StoredFilter {.structSize = D2RL::Inventory::ItemFilterSize, .containerMask = StoredGrids};

auto NoteStack(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* stored = static_cast<Stacks*>(user);
	if (item->code == stored->code && stored->count < MaxStacks) {
		stored->stack[stored->count++] = {item->runtimeId, item->quantity};
	}
	return D2RL::Inventory::IterationAction::Continue;
}

// What the job found: the held stack itself, whole, or the noted stack it merged into
struct Placed {
	items::ItemInfo item;
	int32_t before;
};

auto FindPlaced(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* found = static_cast<Placed*>(user);
	if (item->code != g_job.code) {
		return D2RL::Inventory::IterationAction::Continue;
	}
	if (item->runtimeId == g_job.runtimeId) {
		*found = {*item, 0};
		return D2RL::Inventory::IterationAction::Stop;
	}
	for (uint32_t i = 0; i < g_job.stored.count; ++i) {
		const Stack& s = g_job.stored.stack[i];
		if (s.runtimeId == item->runtimeId && item->quantity == s.quantity + g_job.held) {
			*found = {*item, s.quantity};
			return D2RL::Inventory::IterationAction::Stop;
		}
	}
	return D2RL::Inventory::IterationAction::Continue;
}

void SplitNow(const D2RL::PluginContext*, void*) noexcept {
	const Job& job = g_job;  // stays put until g_busy clears below
	// The UI thread's handle is to the client's unit, stale on the game thread and without seeds, so find the server's
	// unit by its code and unit id, which the two share
	Placed found {};
	const auto listed = g_inventory->forEachInventoryItem(g_context, job.player, &StoredFilter, FindPlaced, &found);
	const items::ItemInfo placed = found.item;
	const uint32_t back = SplitOff(job.code, job.held, true, found.before, placed.quantity, placed.container);
	const D2RL::PlayerHandle player = job.player;
	const int32_t held = job.held;
	g_busy = false;
	if (listed != D2RL::Inventory::Result::Success || back == 0) {
		// Not found (dropped, merged into a stack not noted) or not split (a tome)
		plog::Info("split: none, list %u, %d held, %d found in container %u", static_cast<uint32_t>(listed), held,
		           placed.quantity, static_cast<uint32_t>(placed.container));
		return;
	}
	const items::SplitStackRequest request {.structSize = items::SplitStackRequestSize,
	                                        .player = player,
	                                        .sourceItem = placed.handle,
	                                        .quantity = back};
	items::SplitStackResult result {.structSize = items::SplitStackResultSize};
	const items::Result status = g_items->splitStack(g_context, &request, &result);
	if (status == items::Result::Success) {
		++g_splits;
	} else {
		++g_failed;
	}
	plog::Info("split: code %08x container %u cell %d,%d, %d held onto %d: result %u, %u left down, %u on the cursor",
	           placed.code, static_cast<uint32_t>(placed.container), placed.x, placed.y, held, found.before,
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
		const items::ItemInfo was = g_held;
		g_held = {.structSize = items::ItemInfoSize};
		if (g_items->getItemInfo(g_context, cursor, &g_held) != items::Result::Success) {
			g_held.quantity = 0;
		}
		// Noted once per held stack, before any put-down can merge it
		if (g_held.quantity > 1 && (g_held.runtimeId != was.runtimeId || g_held.quantity != was.quantity)) {
			g_stored = {.code = g_held.code};
			g_inventory->forEachInventoryItem(g_context, player, &StoredFilter, NoteStack, &g_stored);
		}
		return;
	}
	const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	if (g_held.quantity > 1) {
		++g_putDowns;
		g_ctrlPutDowns += ctrl ? 1 : 0;
	}
	if (ctrl && g_held.quantity > 1 && !g_busy.exchange(true)) {
		g_job = {player, g_held.code, g_held.runtimeId, g_held.quantity, g_stored};
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
	// `&`, not `&&`: every service is queried, so the report never blames a missing one by mistake
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
