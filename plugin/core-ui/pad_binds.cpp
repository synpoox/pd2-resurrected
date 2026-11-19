// LB + RB + A on a stack in a stash or stack tab sends the whole stack to the Cube (#1472). The pad's A activates the focused
// item (ItemInteractionService, UI thread, before D2R acts); the move is a game-thread transaction on the server unit, found by code and id as in stack_split.
// shortcut: the SDK emits no shop, merc or "Move 5" event, so those three PD2 binds are not here (docs/controller.md).
#include "pad_binds.h"

#include "controller_split.h"
#include "log.h"

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item_interactions.h>
#include <D2RLPlugin/threads.h>

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
	std::snprintf(report, size, "LB + RB + A sends the whole stack under the pad focus to the Cube");
}

}  // namespace pad_binds
