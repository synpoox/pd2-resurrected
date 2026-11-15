// The Horadric Almanac (IR5, #240), moved to core-rules by #945. PD2's unlimited Tome of Identify (`rid`) identifies every
// unidentified item on inventory page 0 and in the Cube at once, and leaves the stash, worn and belt items.
//
// It used to run from core-maps' item spell hooks (0x581680 and 0x5817f0). Cartographer hooks both functions for its map
// portal, and two plugins cannot own one entry (d2rAI-toolkit knowledge/engine-re.md, "One owner per hook site"), so the
// Almanac leaves them: an ItemInteractionService listener (UI thread) sees the activation before D2R runs the item's
// spell, takes it, and one SDK transaction identifies the items on the game thread.
// The SDK's interaction event names the local player, so a TCP/IP client's use is not served; single player only, as
// the migration's checks are (#942).
#include "almanac.h"

#include "log.h"

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/item_interactions.h>
#include <D2RLPlugin/threads.h>

#include <atomic>
#include <cstdio>
#include <vector>

namespace {

namespace items = D2RL::Items;

constexpr uint32_t CodeAlmanac = items::MakeItemCode('r', 'i', 'd');
constexpr uint32_t Boundary = items::ContainerBit(items::ItemContainer::Inventory) | items::ContainerBit(items::ItemContainer::Cube);

const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
std::atomic<uint32_t> g_uses {0};

struct Pending {
	std::vector<D2RL::ItemHandle> handles;
};

auto Collect(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	if ((item->stateFlags & items::ItemStateIdentified) == 0) {
		static_cast<Pending*>(user)->handles.push_back(item->handle);
	}
	return D2RL::Inventory::IterationAction::Continue;
}

// Game thread: the items of the pressing player, one transaction (all or none, so a refusal changes nothing).
void IdentifyNow(const D2RL::PluginContext*, void* user) noexcept {
	const auto player = reinterpret_cast<D2RL::PlayerHandle>(user);
	Pending pending;
	const D2RL::Inventory::ItemFilter filter {.structSize = D2RL::Inventory::ItemFilterSize, .containerMask = Boundary};
	if (g_inventory->forEachInventoryItem(g_context, player, &filter, Collect, &pending) != D2RL::Inventory::Result::Success) {
		plog::Info("almanac: could not read the inventory");
		return;
	}
	const size_t found = pending.handles.size();
	if (found == 0) {
		plog::Info("almanac: nothing to identify (%u uses)", ++g_uses);
		return;
	}
	std::vector<items::ExistingItemOperation> operations(found);
	for (size_t i = 0; i < found; ++i) {
		operations[i] = {.structSize = sizeof(items::ExistingItemOperation), .kind = items::ExistingItemOperationKind::Edit,
		                 .item = pending.handles[i]};
		operations[i].edit = {.fields = items::EditFieldBit(items::EditField::Identified), .stateFlags = items::ItemStateIdentified};
	}
	const items::ExistingItemTransaction transaction {.structSize = sizeof(items::ExistingItemTransaction),
	                                                  .player = player,
	                                                  .operationCount = static_cast<uint32_t>(found),
	                                                  .operations = operations.data()};
	items::ExistingItemTransactionResult result {.structSize = sizeof(items::ExistingItemTransactionResult)};
	const items::Result status = g_items->executeExistingItemTransaction(g_context, &transaction, &result);
	plog::Info("almanac: %zu items to identify, result %u, %u committed (%u uses)", found, static_cast<uint32_t>(status),
	           result.committedOperationCount, ++g_uses);
}

// UI thread, before D2R handles the activation. Anything but the Almanac goes on to D2R.
auto __cdecl OnActivate(const D2RL::PluginContext*, const D2RL::ItemInteractions::ItemInteractionEvent* event,
                        void*) noexcept -> D2RL::ItemInteractions::Decision {
	using D2RL::ItemInteractions::Decision;
	items::ItemInfo info {.structSize = items::ItemInfoSize};
	if (event == nullptr || event->structSize < D2RL::ItemInteractions::ItemInteractionEventRequiredSize ||
	    g_items->getItemInfo(g_context, event->item, &info) != items::Result::Success || info.code != CodeAlmanac) {
		return Decision::Continue;
	}
	if (g_threads->runOnGameThread(g_context, IdentifyNow, reinterpret_cast<void*>(event->player)) !=
	    D2RL::Threads::Result::Success) {
		plog::Info("almanac: could not queue the identify");
	}
	return Decision::Consume;  // D2R's own use (item spell 18: no handler) must not run on top
}

template <class Service>
auto Query(const D2RL::PluginContext* context, const Service*& service, bool (*has)(const Service*, uint32_t),
           uint32_t required) noexcept -> bool {
	if (context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !has(service, required)) {
		service = nullptr;
	}
	return service != nullptr;
}

}  // namespace

auto InstallAlmanac(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	g_context = context;
	const D2RL::ItemInteractionService* interactions = nullptr;
	const bool services = Query(context, g_items, D2RL::HasItemServiceField, D2RL::ItemServiceRequiredSize) &
	                      Query(context, g_inventory, D2RL::HasInventoryServiceField, D2RL::InventoryServiceRequiredSize) &
	                      Query(context, g_threads, D2RL::HasThreadServiceField, D2RL::ThreadServiceRequiredSize) &
	                      Query(context, interactions, D2RL::HasItemInteractionServiceField,
	                            D2RL::ItemInteractionServiceRequiredSize);
	D2RL::ItemInteractions::ListenerHandle handle = D2RL::ItemInteractions::InvalidHandle;
	const D2RL::ItemInteractions::ItemInteractionListener listener {
		.structSize = D2RL::ItemInteractions::ItemInteractionListenerSize, .callback = OnActivate};
	const bool live = services && interactions->registerListener(context, &listener, &handle) ==
	                                  D2RL::ItemInteractions::Result::Success;
	std::snprintf(report, reportSize, "almanac identify-all%s", live ? "" : " FAILED (a service or the listener is missing)");
	return live ? 1 : 0;
}

auto AlmanacCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "almanac uses %u", g_uses.load());
}
