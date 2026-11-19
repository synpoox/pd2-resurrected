// LB + RB + A on a stack in a stash or stack tab sends the whole stack to the Cube (#1472). The pad's A activates the focused
// item (ItemInteractionService, UI thread, before D2R acts); the move is a game-thread transaction, as the altar's.
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

struct Job {
	D2RL::PlayerHandle player;
	D2RL::ItemHandle item;
	int32_t quantity;
};
Job g_job {};
std::atomic<bool> g_busy {false};

void MoveNow(const D2RL::PluginContext*, void*) noexcept {
	const Job job = g_job;
	g_busy = false;
	items::ExistingItemOperation op {};
	op.structSize = items::ExistingItemOperationSize;
	op.kind = items::ExistingItemOperationKind::Move;
	op.item = job.item;
	op.move.destination = {.structSize = items::ItemDestinationSize, .container = items::ItemContainer::Cube, .placement = items::Placement::Automatic};
	const items::ExistingItemTransaction transaction {
		.structSize = items::ExistingItemTransactionSize, .player = job.player, .operationCount = 1, .operations = &op};
	items::ExistingItemTransactionResult result {.structSize = items::ExistingItemTransactionResultSize};
	const items::Result status = g_items->executeExistingItemTransaction(g_context, &transaction, &result);
	plog::Info("padbinds: stack of %d to the Cube: %u", job.quantity, static_cast<uint32_t>(status));
}

auto __cdecl OnActivate(const D2RL::PluginContext*, const D2RL::ItemInteractions::ItemInteractionEvent* event, void*) noexcept
	-> D2RL::ItemInteractions::Decision {
	using D2RL::ItemInteractions::Decision;
	items::ItemInfo info {.structSize = items::ItemInfoSize};
	const bool chord = event->inputSource == D2RL::ItemInteractions::InputSource::Controller && controller_split::ModifierAndRbHeld();
	plog::Info("padbinds: activate container %u page %d cell %d,%d source %u chord %d", static_cast<uint32_t>(event->container),
	           event->inventoryPage, event->cellX, event->cellY, static_cast<uint32_t>(event->inputSource), chord);
	if (!chord || event->container == items::ItemContainer::Cube ||
	    g_items->getItemInfo(g_context, event->item, &info) != items::Result::Success || info.quantity < 2 || g_busy.exchange(true)) {
		return Decision::Continue;
	}
	g_job = {event->player, event->item, info.quantity};
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
	    context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success) {
		std::snprintf(report, size, "FAILED (an item, item interaction or thread service is missing)");
		return;
	}
	// Stack-tab stacks live in the shared stash or a custom page; opt in to every container but the Cube
	const D2RL::ItemInteractions::ItemInteractionListener listener {
		.structSize = D2RL::ItemInteractions::ItemInteractionListenerSize,
		.priority = 100,
		.callback = OnActivate,
		.containerMask = interactions->supportedContainerMask & ~items::ContainerBit(items::ItemContainer::Cube)};
	D2RL::ItemInteractions::ListenerHandle handle = D2RL::ItemInteractions::InvalidHandle;
	if (interactions->registerListener(context, &listener, &handle) != D2RL::ItemInteractions::Result::Success) {
		std::snprintf(report, size, "FAILED (no item interaction listener)");
		return;
	}
	std::snprintf(report, size, "LB + RB + A sends the whole stack under the pad focus to the Cube");
}

}  // namespace pad_binds
