// LB + RB + A on a stack in a stash or stack tab sends the whole stack to the Cube (#1472). The pad's A activates the focused
// item (ItemInteractionService, UI thread, before D2R acts); the move is a game-thread transaction on the server unit, found by code and id as in stack_split.
// Its stack-tab Activate hook also tops up an inventory stack after a mouse Ctrl+click out of a stack tab (#1775).
// shortcut: the SDK emits no shop, merc or "Move 5" event, so those three PD2 binds are not here (docs/controller.md).
#include "pad_binds.h"

#include "controller_split.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/item_interactions.h>
#include <D2RLPlugin/threads.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>

namespace pad_binds {

namespace {

namespace items = D2RL::Items;

const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;

struct Job {
	D2RL::PlayerHandle player;
	uint32_t code;
	uint32_t runtimeId;
	items::ItemContainer container;
	int32_t quantity;
};
Job g_job {};
std::atomic<bool> g_busy {false};

// The UI thread's handle is the client's unit, stale on the game thread (as in stack_split.cpp): find the server's unit
// by its code and unit id, which the two share
auto FindItem(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	if (item->code == g_job.code && item->runtimeId == g_job.runtimeId) {
		*static_cast<D2RL::ItemHandle*>(user) = item->handle;
		return D2RL::Inventory::IterationAction::Stop;
	}
	return D2RL::Inventory::IterationAction::Continue;
}

void MoveNow(const D2RL::PluginContext*, void*) noexcept {
	const Job job = g_job;
	D2RL::ItemHandle found = D2RL::InvalidItemHandle;
	const D2RL::Inventory::ItemFilter filter {.structSize = D2RL::Inventory::ItemFilterSize, .containerMask = items::ContainerBit(job.container)};
	g_inventory->forEachInventoryItem(g_context, job.player, &filter, FindItem, &found);
	g_busy = false;
	if (found == D2RL::InvalidItemHandle) {
		plog::Info("padbinds: stack of %d to the Cube failed: item not found", job.quantity);
		return;
	}
	items::ExistingItemOperation op {};
	op.structSize = items::ExistingItemOperationSize;
	op.kind = items::ExistingItemOperationKind::Move;
	op.item = found;
	op.move.destination = {.structSize = items::ItemDestinationSize, .container = items::ItemContainer::Cube, .placement = items::Placement::Automatic};
	const items::ExistingItemTransaction transaction {
		.structSize = items::ExistingItemTransactionSize, .player = job.player, .operationCount = 1, .operations = &op};
	items::ExistingItemTransactionResult result {.structSize = items::ExistingItemTransactionResultSize};
	const items::Result status = g_items->executeExistingItemTransaction(g_context, &transaction, &result);
	plog::Info("padbinds: stack of %d to the Cube %s (%u)", job.quantity, status == items::Result::Success ? "moved" : "failed",
	           static_cast<uint32_t>(status));
}

auto __cdecl OnActivate(const D2RL::PluginContext*, const D2RL::ItemInteractions::ItemInteractionEvent* event, void*) noexcept
	-> D2RL::ItemInteractions::Decision {
	using D2RL::ItemInteractions::Decision;
	items::ItemInfo info {.structSize = items::ItemInfoSize};
	if (event->inputSource != D2RL::ItemInteractions::InputSource::Controller || !controller_split::ModifierAndRbHeld()) {
		return Decision::Continue;
	}
	plog::Info("padbinds: chord on container %u page %d cell %d,%d", static_cast<uint32_t>(event->container), event->inventoryPage,
	           event->cellX, event->cellY);
	if (g_items->getItemInfo(g_context, event->item, &info) != items::Result::Success || info.quantity < 2 || g_busy.exchange(true)) {
		return Decision::Continue;
	}
	g_job = {event->player, info.code, info.runtimeId, event->container, info.quantity};
	if (g_threads->runOnGameThread(g_context, MoveNow, nullptr) != D2RL::Threads::Result::Success) {
		g_busy = false;
		return Decision::Continue;
	}
	return Decision::Consume;
}

// A on a Runes, Gems or other stack-tab cell never reaches the item interaction events: D2R's AdvancedStashSlotWidget::Activate
// (93847 0x2cf760, widget, focus) moves the item itself. No SDK service covers it, so it is hooked by signature here (#1515).
using ActivateFn = void (*)(void*, uint64_t) noexcept;
ActivateFn g_activate = nullptr;
constexpr uint8_t ActivateEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57};
// 93847 0x2cf590 (widget, unused, mode 0 = inventory, 3 = Cube, count): the bulk move D2R's hold-Y calls
using BulkFn = void (*)(void*, void*, uint8_t, uint8_t) noexcept;
BulkFn g_bulk = nullptr;
constexpr size_t WidgetItem = 0x608;

// #1775: Ctrl+click on a stack-tab cell (0x2cf760's Ctrl branch: vtable 0xd8, mode 0) takes one out as a new single into a free
// inventory cell (stash_stacks.cpp: D2R's withdraw lowers the slot count by one) and never tops up an inventory stack. So the
// click notes the inventory's items and the slot item's class; the next game updates merge a new item of that class onto the
// first inventory item of it that editItem lets take it (misc.txt maxstack, 50 for every stack-tab stackable), then destroy it.
// Only counts change: the stack takes the sum, read back natively (#1628: ItemInfo.quantity misreads PD2 stackables), before
// the single goes; a failed destroy puts the stack's count back. The SDK sees no stack tab, so it cannot do the take itself.
// shortcut: a click on another tab's class while one is pending drops the first's top-up (its single stays in its cell).
using KeyFn = int (*)(int) noexcept;  // D2R's "is this virtual key down" (93847 0x120a100), as 0x2cf760 reads Ctrl and Shift
KeyFn g_key = nullptr;
constexpr uint32_t MaxCarried = 128;   // an inventory holds at most 10 x 8 items
constexpr uint32_t TopUpUpdates = 50;  // game updates (25 a second) the top-up waits for the server's take
struct TopUp {
	bool on = false;
	uint32_t classId = 0;
	uint32_t updates = 0;
	uint32_t count = 0;
	uint32_t ids[MaxCarried] {};  // the inventory's items before the take; a merged-into or tried item joins them
};
TopUp g_topUp;
SRWLOCK g_topUpLock = SRWLOCK_INIT;

constexpr auto Listed(const uint32_t* ids, uint32_t count, uint32_t id) noexcept -> bool {
	for (uint32_t i = 0; i < count; ++i) {
		if (ids[i] == id) {
			return true;
		}
	}
	return false;
}
constexpr uint32_t ListedTest[] = {7, 9};
static_assert(Listed(ListedTest, 2, 9) && !Listed(ListedTest, 2, 8) && !Listed(ListedTest, 0, 7));

constexpr D2RL::Inventory::ItemFilter InventoryOnly {.structSize = D2RL::Inventory::ItemFilterSize,
                                                     .containerMask = items::ContainerBit(items::ItemContainer::Inventory)};

auto NoteId(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* snap = static_cast<TopUp*>(user);
	if (snap->count < MaxCarried) {
		snap->ids[snap->count++] = item->runtimeId;
	}
	return D2RL::Inventory::IterationAction::Continue;
}

struct Carried {
	uint32_t classId;
	uint32_t count = 0;
	items::ItemInfo item[MaxCarried];
};
auto NoteClass(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* carried = static_cast<Carried*>(user);
	if (item->classId == carried->classId && carried->count < MaxCarried) {
		carried->item[carried->count++] = *item;
	}
	return D2RL::Inventory::IterationAction::Continue;
}

void ReadQuantity(const D2RL::PluginContext*, void* native, void* user) noexcept {
	*static_cast<int32_t*>(user) =
	    d2r::Engine<int32_t (*)(d2r::Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat)(static_cast<d2r::Unit*>(native), 70, 0);
}
auto Quantity(D2RL::ItemHandle item) noexcept -> int32_t {
	int32_t count = 0;
	return g_items->editNativeItem(g_context, item, ReadQuantity, &count) == items::Result::Success ? count : 0;
}
auto SetQuantity(D2RL::PlayerHandle player, D2RL::ItemHandle item, int32_t count) noexcept -> bool {
	const items::ItemEdit edit {.structSize = items::ItemEditSize, .fields = items::EditFieldBit(items::EditField::Quantity),
	                            .quantity = static_cast<uint32_t>(count)};
	return g_items->editItem(g_context, player, item, &edit) == items::Result::Success && Quantity(item) == count;
}

// The single onto a stack; true when it is gone into the stack
auto Merge(D2RL::PlayerHandle player, const items::ItemInfo& single, const items::ItemInfo& stack) noexcept -> bool {
	const int32_t have = Quantity(stack.handle), add = Quantity(single.handle);
	if (have < 1 || add < 1 || !SetQuantity(player, stack.handle, have + add)) {
		if (have >= 1 && Quantity(stack.handle) != have && !SetQuantity(player, stack.handle, have)) {
			plog::Warn("padbinds: top-up of %08x: the stack's count could not be put back to %d", stack.code, have);
		}
		return false;  // past maxstack (editItem refuses) or unread: the next stack, or it stays in its cell
	}
	const items::Result gone = g_items->destroyItem(g_context, player, single.handle, items::SocketedItemPolicy::RejectIfNotEmpty);
	if (gone != items::Result::Success) {
		const bool back = SetQuantity(player, stack.handle, have);
		(back ? plog::Info : plog::Warn)("padbinds: top-up of %08x: destroy failed (%u), the stack %s %d", stack.code,
		                                 static_cast<uint32_t>(gone), back ? "is back to" : "COULD NOT be put back to", have);
		return false;
	}
	plog::Info("padbinds: %08x x%d taken out of the stack tab topped up the inventory stack at %d,%d (%d -> %d)", single.code, add,
	           stack.x, stack.y, have, have + add);
	return true;
}

// Game thread: each update until TopUpUpdates pass, a new item of the class merges onto a stack of it
void TopUpNow(const D2RL::PluginContext*, void*) noexcept {
	AcquireSRWLockExclusive(&g_topUpLock);
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	static Carried carried;  // game thread only; too big for the stack
	carried.classId = g_topUp.classId;
	carried.count = 0;
	if (g_topUp.on && g_inventory->getLocalPlayer(g_context, &player) == D2RL::Inventory::Result::Success &&
	    g_inventory->forEachInventoryItem(g_context, player, &InventoryOnly, NoteClass, &carried) == D2RL::Inventory::Result::Success) {
		for (uint32_t i = 0; i < carried.count; ++i) {
			const items::ItemInfo& single = carried.item[i];
			if (Listed(g_topUp.ids, g_topUp.count, single.runtimeId)) {
				continue;
			}
			bool merged = false;
			for (uint32_t j = 0; j < carried.count && !merged; ++j) {
				const items::ItemInfo& stack = carried.item[j];
				merged = j != i && Listed(g_topUp.ids, g_topUp.count, stack.runtimeId) && Merge(player, single, stack);
			}
			if (!merged && g_topUp.count < MaxCarried) {
				g_topUp.ids[g_topUp.count++] = single.runtimeId;  // a stack now, for the next single
				plog::Info("padbinds: %08x taken out of the stack tab: no inventory stack of it takes it, it keeps its cell", single.code);
			}
		}
	}
	if (g_topUp.on && ++g_topUp.updates < TopUpUpdates) {
		g_topUp.on = g_threads->runOnGameThread(g_context, TopUpNow, nullptr) == D2RL::Threads::Result::Success;
	} else {
		g_topUp.on = false;
	}
	ReleaseSRWLockExclusive(&g_topUpLock);
}

// UI thread, before D2R's Ctrl+click take of the slot's item
void NoteTake(D2RL::PlayerHandle player, void* slotItem) noexcept {
	static TopUp snap;  // UI thread only
	snap = {};
	snap.on = true;
	snap.classId = static_cast<uint32_t>(
	    d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(d2r::sig::UnitClassOf)(slotItem, __FILE__, __LINE__));
	if (g_inventory->forEachInventoryItem(g_context, player, &InventoryOnly, NoteId, &snap) != D2RL::Inventory::Result::Success) {
		return;
	}
	AcquireSRWLockExclusive(&g_topUpLock);
	if (g_topUp.on && g_topUp.classId == snap.classId) {
		g_topUp.updates = 0;  // another take of the same class: wait for it too
	} else {
		const bool queued = g_topUp.on;
		g_topUp = snap;
		if (!queued) {
			g_topUp.on = g_threads->runOnGameThread(g_context, TopUpNow, nullptr) == D2RL::Threads::Result::Success;
		}
	}
	ReleaseSRWLockExclusive(&g_topUpLock);
}

void OnActivateCell(void* widget, uint64_t focus) noexcept {
	void* const slotItem = *reinterpret_cast<void**>(static_cast<char*>(widget) + WidgetItem);
	if (g_key != nullptr && slotItem != nullptr && d2r::sig::UnitClassOf.rva != 0 && d2r::sig::UnitStat.rva != 0 && g_key(0x11) != 0 &&
	    g_key(0x10) == 0) {
		D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
		D2RL::ItemHandle cursor = D2RL::InvalidItemHandle;
		if (g_inventory->getLocalPlayer(g_context, &player) == D2RL::Inventory::Result::Success &&
		    g_inventory->getCursorItem(g_context, player, &cursor) == D2RL::Inventory::Result::NotFound) {
			NoteTake(player, slotItem);
		}
	}
	if (g_bulk != nullptr && slotItem != nullptr) {
		const bool whole = controller_split::ModifierAndRbHeld();
		if (whole || controller_split::RbAloneHeld()) {
			D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
			D2RL::ItemHandle cursor = D2RL::InvalidItemHandle;
			// With an item on the cursor A places it: stock path. Only NotFound means an empty cursor; any other error is stock too.
			// A mouse click on a stack cell while RB is held also takes this path (shortcut: not told apart from the pad's A).
			if (g_inventory->getLocalPlayer(g_context, &player) == D2RL::Inventory::Result::Success &&
			    g_inventory->getCursorItem(g_context, player, &cursor) == D2RL::Inventory::Result::NotFound) {
				plog::Info("padbinds: stack tab %s", whole ? "whole stack to the Cube" : "5 to the inventory");
				g_bulk(widget, nullptr, whole ? 3 : 0, whole ? 255 : 5);
				return;
			}
		}
	}
	g_activate(widget, focus);
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	g_context = context;
	const D2RL::ItemInteractionService* interactions = nullptr;
	if (context->QueryService(&interactions) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasItemInteractionServiceField(interactions, D2RL::ItemInteractionServiceRequiredSize) ||
	    context->QueryService(&g_items) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasItemServiceField(g_items, D2RL::ItemServiceSplitStackFieldEnd) ||
	    context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
	    context->QueryService(&g_inventory) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasInventoryServiceField(g_inventory, D2RL::InventoryServiceRequiredSize)) {
		std::snprintf(report, size, "FAILED (an item, item interaction, inventory or thread service is missing)");
		return;
	}
	// The stash and stack tab only; the shared stash bit needs the loader's supportedContainerMask field
	uint32_t mask = items::ContainerBit(items::ItemContainer::PersonalStash) | items::ContainerBit(items::ItemContainer::CustomPage);
	if (D2RL::HasItemInteractionServiceField(interactions, D2RL::ItemInteractionServiceSupportedContainerMaskFieldEnd)) {
		mask |= interactions->supportedContainerMask & items::ContainerBit(items::ItemContainer::SharedStash);
	}
	const D2RL::ItemInteractions::ItemInteractionListener listener {
		.structSize = D2RL::ItemInteractions::ItemInteractionListenerSize,
		.priority = 100,
		.callback = OnActivate,
		.containerMask = mask};
	D2RL::ItemInteractions::ListenerHandle handle = D2RL::ItemInteractions::InvalidHandle;
	if (interactions->registerListener(context, &listener, &handle) != D2RL::ItemInteractions::Result::Success) {
		std::snprintf(report, size, "FAILED (no item interaction listener)");
		return;
	}
	uint64_t bulk = 0;
	const bool hooked =
	    sigscan::ScanUnique(context->exeBase,
	                        "48 89 5c 24 10 48 89 6c 24 18 48 89 74 24 20 57 48 83 ec 30 48 8b b1 08 06 00 00", 0, &bulk) == 1 &&
	    sigscan::HookBySignature(context, "StackTabActivate", "48 89 5c 24 18 48 89 54 24 10 55 56 57 41 56 41 57", 0, ActivateEntry,
	                             sizeof ActivateEntry, OnActivateCell, &g_activate);
	if (hooked) {
		g_bulk = reinterpret_cast<BulkFn>(context->exeBase + bulk);
	} else {
		plog::Warn("padbinds: stack tab RB + A and the Ctrl+click top-up are off (signature not found)");
	}
	sigscan::Entry* const sigs[] = {&d2r::sig::UnitClassOf, &d2r::sig::UnitStat};  // the top-up's class and count reads (#1775)
	sigscan::Resolve(sigs, 2, context->exeBase, nullptr, 0);
	uint64_t key = 0;
	if (sigscan::ScanUnique(context->exeBase, "48 83 ec 28 ff 15 ?? ?? ?? ?? c1 e8 0f", 0, &key) == 1) {
		g_key = reinterpret_cast<KeyFn>(context->exeBase + key);
	}
	if (g_key == nullptr || d2r::sig::UnitClassOf.rva == 0 || d2r::sig::UnitStat.rva == 0) {
		plog::Warn("padbinds: Ctrl+click out of a stack tab does not top up inventory stacks (signature not found)");
	}
	std::snprintf(report, size, "LB + RB + A sends the whole stack under the pad focus to the Cube");
}

}  // namespace pad_binds
