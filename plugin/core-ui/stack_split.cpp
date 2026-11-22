// The split, all through the PluginSDK (no D2R hook): the SDK has no mouse event and no event for an item put down
// from the cursor (ItemInteractionService skips cursor paths), so a UI-thread watch sees the cursor each update. When a
// stack of N > 1 leaves it with Ctrl down, a game-thread job finds that same stack, whole, in a stored grid and
// splitStack takes N-1 of it back to the cursor. D2R itself chose the cell and the panel (inventory, cube, stash), and
// splitStack moves quantity between the stack and a new cursor stack in one step, so nothing is lost or made twice.
// PD2 instead sends its own packet 0x22 and makes a new 1-quantity item at the cell (docs/research/items/stack-split.md);
// the result is the same, but the one item left down here is the original and the cursor stack is the new one.
// In core-ui (client-only, ADR 0004) though it changes items: the result is what a put-down and a pick-up would give.
//
// shortcut: a TCP/IP client has no game thread (runOnGameThread is Unavailable), so it gets no split; upgrade with a
// plugin message to the host if online play needs it.
// A Ctrl+click on a stored stack of the same item (#362): D2R merges the whole held stack into it, so while a stack is
// held the watch notes the stored stacks with its code, and the job splits N-1 back off the one that rose by N.
// shortcut: a merge that stops at the stack cap leaves the rest on the cursor, and that is kept as D2R does it (PD2
// would add one); upgrade if players miss it.
// Pad: Controller QoL's item modifier (LB by default) held with A is the Ctrl of a pad (controller_split.cpp).
// shortcut: the job runs on the next game update and looks once; a server that has not placed the item by then skips
// the split (logged), fine on a local game, upgrade with a retry if hosted games miss splits.
// Shared stash (#1820): Loader 1.3.1 reads its stacks' quantity as 0 and refuses splitStack there (Unsupported), so the
// job reads the count off the native item (stat 70, UnitStat by signature) and splits through the inventory: the stack
// moves there (executeExistingItemTransaction, SharedStashWrite), splitStack takes N-1 to the cursor, and the rest moves
// back to its page and cell. No room in the inventory: no split, the stack stays down whole.
#include "stack_split.h"

#include "controller_split.h"
#include "log.h"
#include "map_stack.h"
#include "pd2_engine.h"
#include "sig_hook.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/threads.h>
#include <D2RLPlugin/widgets.h>

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
static_assert(RefusedByType([](int32_t t) { return t == TypeThrown; }));   // javelins, throwing knives and potions
static_assert(RefusedByType([](int32_t t) { return t == TypeMissile; }));  // arrows, bolts
static_assert(!RefusedByType([](int32_t t) { return t == 75; }));          // a rune still splits

const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;  // optional: without it the pad legend stays hidden

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
};
Job g_job {};
// The stored stacks noted for a held stack: the UI thread notes them first, a game job then fills the counts the UI
// could not read (the shared stash's, a native game-thread call), and the split job, queued after it, reads them
std::atomic<D2RL::PlayerHandle> g_notePlayer {D2RL::InvalidPlayerHandle};
std::atomic<uint32_t> g_noteCode {0};
Stacks g_stored {};  // both threads, under g_storedLock (no SDK call while it is held)
SRWLOCK g_storedLock = SRWLOCK_INIT;
std::atomic<bool> g_busy {false};
bool g_watching = false;  // the UI-thread watch is queued (UI thread only)
std::atomic<uint32_t> g_splits {0}, g_failed {0};
uint32_t g_ticks = 0, g_putDowns = 0, g_ctrlPutDowns = 0;  // UI thread only; the status line reads them racily

constexpr D2RL::Inventory::ItemFilter StoredFilter {.structSize = D2RL::Inventory::ItemFilterSize, .containerMask = StoredGrids};

// The stored stacks with one code, as the SDK lists them (game thread)
struct Listed {
	uint32_t code;
	uint32_t count;
	items::ItemInfo item[MaxStacks];
};

auto ListCode(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* listed = static_cast<Listed*>(user);
	if (item->code == listed->code && listed->count < MaxStacks) {
		listed->item[listed->count++] = *item;
	}
	return D2RL::Inventory::IterationAction::Continue;
}

void ReadQuantity(const D2RL::PluginContext*, void* native, void* user) noexcept {
	*static_cast<int32_t*>(user) = d2r::Engine<int32_t (*)(d2r::Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat)(
	    static_cast<d2r::Unit*>(native), d2r::rva::StatQuantity, 0);
}

// A stack's count; the SDK reads 0 for a shared stash stack (#1820), so that one is read off the native item (game
// thread). 0 when neither read works.
auto Quantity(const items::ItemInfo& item) noexcept -> int32_t {
	int32_t count = item.quantity;
	if (count == 0 && d2r::sig::UnitStat.state == sigscan::State::Ok &&
	    g_items->editNativeItem(g_context, item.handle, ReadQuantity, &count) != items::Result::Success) {
		count = 0;
	}
	return count;
}

// The stored stacks of the held code, before any put-down can merge it. The UI thread notes them first, with the SDK's
// counts (0 for a shared stash stack), so a fast put-down cannot beat the note; the game job then fills only the counts
// the UI read as 0, and keeps the UI's pre-merge counts.
void NoteStored(uint32_t code, D2RL::PlayerHandle player, bool game) noexcept {
	static Listed uiListed, gameListed;  // one per thread
	Listed& listed = game ? gameListed : uiListed;
	listed = {.code = code};
	g_inventory->forEachInventoryItem(g_context, player, &StoredFilter, ListCode, &listed);
	Stacks old {};
	if (game) {
		AcquireSRWLockShared(&g_storedLock);
		old = g_stored;
		ReleaseSRWLockShared(&g_storedLock);
	}
	Stacks noted {.code = code};
	for (uint32_t i = 0; i < listed.count && noted.count < MaxStacks; ++i) {
		const items::ItemInfo& item = listed.item[i];
		int32_t count = item.quantity;
		if (game) {
			count = Quantity(item);
			for (uint32_t j = 0; j < old.count; ++j) {
				if (old.code == code && old.stack[j].runtimeId == item.runtimeId && old.stack[j].quantity != 0) {
					count = old.stack[j].quantity;
				}
			}
		}
		noted.stack[noted.count++] = {item.runtimeId, count};
	}
	AcquireSRWLockExclusive(&g_storedLock);
	g_stored = noted;
	ReleaseSRWLockExclusive(&g_storedLock);
}

void NoteNow(const D2RL::PluginContext*, void*) noexcept {
	NoteStored(g_noteCode.load(), g_notePlayer.load(), true);
}

// What the job found: the held stack itself, whole, or the noted stack it merged into
struct Placed {
	items::ItemInfo item;
	int32_t before;
	int32_t quantity;
};

// The counts are read after the listing: the native read is a second SDK call, kept out of the iteration callback
auto FindPlaced(const Listed& listed) noexcept -> Placed {
	AcquireSRWLockShared(&g_storedLock);
	const Stacks stored = g_stored;
	ReleaseSRWLockShared(&g_storedLock);
	for (uint32_t i = 0; i < listed.count; ++i) {
		const items::ItemInfo& item = listed.item[i];
		if (item.runtimeId == g_job.runtimeId) {
			return {item, 0, Quantity(item)};
		}
		for (uint32_t j = 0; j < stored.count; ++j) {
			const Stack& s = stored.stack[j];
			if (s.runtimeId == item.runtimeId && stored.code == g_job.code) {
				if (const int32_t now = Quantity(item); now == s.quantity + g_job.held) {
					return {item, s.quantity, now};
				}
			}
		}
	}
	return {};
}

auto Move(D2RL::PlayerHandle player, D2RL::ItemHandle item, const items::ItemDestination& to) noexcept -> items::Result {
	items::ExistingItemOperation op {.structSize = items::ExistingItemOperationSize, .kind = items::ExistingItemOperationKind::Move, .item = item};
	op.move.destination = to;
	const items::ExistingItemTransaction transaction {
		.structSize = items::ExistingItemTransactionSize, .player = player, .operationCount = 1, .operations = &op};
	items::ExistingItemTransactionResult result {.structSize = items::ExistingItemTransactionResultSize};
	return g_items->executeExistingItemTransaction(g_context, &transaction, &result);
}

constexpr items::ItemDestination ToInventory {
	.structSize = items::ItemDestinationSize, .container = C::Inventory, .placement = items::Placement::Automatic};

auto BackTo(const items::ItemInfo& at) noexcept -> items::ItemDestination {
	return {.structSize = items::ItemDestinationSize, .container = C::SharedStash, .placement = items::Placement::Exact,
	        .x = static_cast<uint32_t>(at.x), .y = static_cast<uint32_t>(at.y), .sharedStashPage = at.sharedStashPage};
}

// splitStack copies no stats; the piece is made with its own Area Level, so only the forced event (stat 553) is set again:
// read off the source before the split, set on the piece after (two callbacks, never both units at once)
void CollectForcedEvent(const D2RL::PluginContext*, void* native, void* user) noexcept {
	auto& saved = *static_cast<pd2::mapstack::Saved*>(user);
	pd2::mapstack::Collect(static_cast<d2r::Unit*>(native), d2r::rva::ItemStatCostRows, d2r::rva::StatQuantity,
	                       d2r::Engine<int32_t (*)(d2r::Unit*, uint32_t, pd2::mapstack::Entry*, int32_t) noexcept>(d2r::rva::UnitStatEntries), saved);
	int32_t kept = 0;
	for (int32_t i = 0; i < saved.count; ++i) {
		if (saved.entry[i].stat == d2r::rva::StatMapForceEvent) {
			saved.entry[kept++] = saved.entry[i];
		}
	}
	saved.count = kept;
}

void ApplyNative(const D2RL::PluginContext*, void* native, void* user) noexcept {
	pd2::mapstack::Apply(static_cast<d2r::Unit*>(native), *static_cast<const pd2::mapstack::Saved*>(user),
	                     [](d2r::Unit* unit, int32_t stat, int32_t value, int32_t layer) noexcept {
		                     d2r::Engine<void (*)(d2r::Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::rva::SetUnitStat)(unit, stat, value, layer);
	                     });
}

// Native: the SDK's ItemInfo has no item type; ItemIsType walks the type's parents as PD2's test does
void CheckRefused(const D2RL::PluginContext*, void* native, void* user) noexcept {
	const auto isType = d2r::Engine<int32_t (*)(d2r::Unit*, int32_t) noexcept>(d2r::sig::ItemIsType);
	*static_cast<bool*>(user) = RefusedByType([&](int32_t type) { return isType(static_cast<d2r::Unit*>(native), type) != 0; });
}

void SplitNow(const D2RL::PluginContext*, void*) noexcept {
	const Job& job = g_job;  // stays put until g_busy clears below
	// The UI thread's handle is to the client's unit, stale on the game thread and without seeds, so find the server's
	// unit by its code and unit id, which the two share
	static Listed stacks;
	stacks = {.code = job.code};
	const auto listed = g_inventory->forEachInventoryItem(g_context, job.player, &StoredFilter, ListCode, &stacks);
	const Placed found = FindPlaced(stacks);
	items::ItemInfo placed = found.item;
	const uint32_t back = SplitOff(job.code, job.held, true, found.before, found.quantity, placed.container);
	const D2RL::PlayerHandle player = job.player;
	const int32_t held = job.held;
	g_busy = false;
	if (listed != D2RL::Inventory::Result::Success || back == 0) {
		// Not found (dropped, merged into a stack not noted) or not split (a tome)
		plog::Info("split: none, list %u, %d held, %d found in container %u", static_cast<uint32_t>(listed), held,
		           found.quantity, static_cast<uint32_t>(placed.container));
		return;
	}
	const bool shared = placed.container == C::SharedStash;
	const items::ItemInfo home = placed;
	bool refused = true;  // stays true (no split) if the item cannot be read
	g_items->editNativeItem(g_context, placed.handle, CheckRefused, &refused);
	if (refused) {
		plog::Info("split: refused, code %08x is a throwable or ammo (PD2 never splits those), %d stays whole", placed.code,
		           found.quantity);
		return;
	}
	// splitStack refuses the shared stash (#1820): the stack goes to the inventory first, the same item and handle
	if (shared) {
		const items::Result out = D2RL::HasItemServiceCapability(g_items, items::ItemServiceCapability::SharedStashWrite)
		                              ? Move(player, placed.handle, ToInventory)
		                              : items::Result::Unsupported;
		if (out != items::Result::Success) {
			++g_failed;
			plog::Info("split: refused, could not move it to the inventory (%u), %d stays whole on page %u cell %d,%d",
			           static_cast<uint32_t>(out), found.quantity, home.sharedStashPage, home.x, home.y);
			return;
		}
		placed.container = C::Inventory;
	}
	// Only the forced event is set again: the piece already has its Area Level, and setting that on top doubled it on the
	// server (87 -> 174), so the piece would not stack until a reload (#1499)
	const bool isMap = pd2::mapstack::IsMapCode(placed.code);
	pd2::mapstack::Saved saved;
	if (isMap) {
		g_items->editNativeItem(g_context, placed.handle, CollectForcedEvent, &saved);
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
	if (shared) {
		// What stays down goes home: the one item, or the whole stack when the split failed
		const items::Result returned = Move(player, placed.handle, BackTo(home));
		g_failed += returned == items::Result::Success ? 0 : 1;
		plog::Info("split: shared stash page %u cell %d,%d via the inventory, back home %u%s", home.sharedStashPage, home.x,
		           home.y, static_cast<uint32_t>(returned), returned == items::Result::Success ? "" : " (left in the inventory)");
	}
	if (isMap && saved.count > 0 && status == items::Result::Success) {
		const auto applied = g_items->editNativeItem(g_context, result.cursorItem, ApplyNative, &saved);
		plog::Info("split: set the forced event (%d entries) on the new map piece (%u)", saved.count, static_cast<uint32_t>(applied));
	}
	plog::Info("split: code %08x container %u cell %d,%d, %d held onto %d: result %u, %u left down, %u on the cursor",
	           placed.code, static_cast<uint32_t>(placed.container), placed.x, placed.y, held, found.before,
	           static_cast<uint32_t>(status), result.remainingQuantity, back);
}

// The pad legend ("LB + A Split Stack", #387, controller/playerinventoryexpansionlayouthd.json) shows while a splittable stack is held.
// The layout's "visible": false is not honored, and a layout switch or panel rebuild makes new widgets (maybe under the same panel
// handle), so both widgets are looked up each update and set when the state or either handle differs from what was last applied.
// A failed lookup counts as not applied and is retried next update (#1704). Sets only on a change: no per-frame set, no log.
void UpdateLegend(bool want) noexcept {
	static bool applied = false;
	static D2RL::Widgets::WidgetHandle appliedWidgets[2] = {D2RL::Widgets::InvalidHandle, D2RL::Widgets::InvalidHandle};
	if (g_widgets == nullptr) {
		return;
	}
	D2RL::Widgets::WidgetHandle panel = D2RL::Widgets::InvalidHandle;
	for (const char* name : {"PlayerInventoryExpansionLayout", "PlayerInventoryOriginalLayout"}) {
		if (g_widgets->findPanel(g_context, name, &panel) == D2RL::Widgets::Result::Success) {
			break;
		}
		panel = D2RL::Widgets::InvalidHandle;
	}
	if (panel == D2RL::Widgets::InvalidHandle) {
		appliedWidgets[0] = appliedWidgets[1] = D2RL::Widgets::InvalidHandle;  // closed: the next open has new widgets
		return;
	}
	const char* const names[2] = {"pd2rSplitLegendLB", "pd2rSplitLegend"};
	for (int i = 0; i < 2; ++i) {
		D2RL::Widgets::WidgetHandle widget = D2RL::Widgets::InvalidHandle;
		if (g_widgets->findWidget(g_context, panel, names[i], &widget) != D2RL::Widgets::Result::Success) {
			widget = D2RL::Widgets::InvalidHandle;  // not applied: retried next update
		} else if (widget != appliedWidgets[i] || want != applied) {
			g_widgets->setWidgetVisible(g_context, widget, want);
		}
		appliedWidgets[i] = widget;
	}
	applied = want;
}

void Watch(const D2RL::PluginContext*, void*) noexcept {
	g_watching = g_threads->runOnUiThread(g_context, Watch, nullptr) == D2RL::Threads::Result::Success;  // again next update
	++g_ticks;
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	D2RL::ItemHandle cursor = D2RL::InvalidItemHandle;
	if (g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success) {
		g_held.quantity = 0;
		UpdateLegend(false);
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
			g_notePlayer = player;
			g_noteCode = g_held.code;
			NoteStored(g_noteCode, player, false);
			// queued before any split job of this stack
			if (const auto queued = g_threads->runOnGameThread(g_context, NoteNow, nullptr); queued != D2RL::Threads::Result::Success) {
				plog::Info("split: could not queue the note (%u)", static_cast<uint32_t>(queued));
			}
		}
		UpdateLegend(Splittable(g_held.code, g_held.quantity));
		return;
	}
	UpdateLegend(false);
	const bool ctrl = g_held.quantity > 1 && ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 || controller_split::ModifierHeld());
	if (g_held.quantity > 1) {
		++g_putDowns;
		g_ctrlPutDowns += ctrl ? 1 : 0;
	}
	if (ctrl && !g_busy.exchange(true)) {
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
	// d2r::Bind resolved it (with the build cache: core-rules hooks its entry, so a fresh scan would miss it)
	if (d2r::sig::ItemIsType.state != sigscan::State::Ok) {
		std::snprintf(report, size, "FAILED (signature ItemIsType %s)", sigscan::StateText(d2r::sig::ItemIsType.state));
		return;
	}
	Query(g_widgets, D2RL::HasWidgetServiceField, D2RL::WidgetServiceRequiredSize);  // optional, see g_widgets
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
	std::snprintf(report, size, "Ctrl+left-click (or Controller QoL's modifier, LB by default, and A on a pad) puts one item of a held stack down");
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "stack splits %u (failed %u); watch updates %u, stacks put down %u (%u with Ctrl)", g_splits.load(),
	              g_failed.load(), g_ticks, g_putDowns, g_ctrlPutDowns);
}

}  // namespace stack_split
