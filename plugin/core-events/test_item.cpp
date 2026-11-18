// Game-check aids: `core-events corrupt` and `core-events desecrate` act on the cursor item, else the inventory item at
// cell (0,0), so a check needs no cube or altar-panel clicks (#1369). Both call the code the cube and the altar call
// (CorruptFreshItem, Desecrate): no second copy of the rolls. The SDK names the local player on the UI thread only and
// the rolls write the item, so the command hops UI thread -> game thread (item reads and editNativeItem).
// shortcut: single player only (the local player), like test_kill.cpp.
#include "corruption_roll.h"
#include "log.h"
#include "lucion.h"
#include "test_kill.h"

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/threads.h>

#include <cstdio>

namespace {

namespace items = D2RL::Items;
namespace inv = D2RL::Inventory;
using d2r::At;
using d2r::Game;
using d2r::Unit;
using Kind = pd2::test_cmd::Cmd::Kind;

const D2RL::PluginContext* g_context = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

struct Job {
	Kind kind;
	Game* game;
	Unit* player;
};

auto FindCellZero(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> inv::IterationAction {
	if (item->x == 0 && item->y == 0) {
		*static_cast<D2RL::ItemHandle*>(user) = item->handle;
		return inv::IterationAction::Stop;
	}
	return inv::IterationAction::Continue;
}

// Game thread: on the native item
void Act(const D2RL::PluginContext*, void* native, void* user) noexcept {
	using namespace d2r::rva;
	const auto* job = static_cast<const Job*>(user);
	auto* item = static_cast<Unit*>(native);
	if (job->kind == Kind::Corrupt) {
		const bool done = CorruptFreshItem(job->game, item);
		plog::Info("test aid: corrupt: item %d %s", At<int32_t>(item, UnitClassId),
		           done ? "corrupted (the normal roll)" : "NOT corrupted (already corrupted, or no row matches)");
		if (!done) {
			return;
		}
	} else {
		// Desecrate wants Lucion dead (stage 8) and sets 9 itself; a repeat attempt on the same game needs 8 again
		const uint32_t key = LucionStageKey;
		*d2r::Engine<int32_t* (*)(uint8_t*, uint32_t*) noexcept>(GameCounter)(reinterpret_cast<uint8_t*>(job->game) + GameCounters,
		                                                                   const_cast<uint32_t*>(&key)) = LucionDeadStage;
		const DesecrateResult result = Desecrate(job->game, job->player, item);
		plog::Info("test aid: desecrate: item %d %s", At<int32_t>(item, UnitClassId),
		           result == DesecrateResult::Desecrated ? "desecrated" : result == DesecrateResult::Failed ? "roll failed (no brick here)"
		                                                                                                    : "refused, see the line above");
		return;  // Desecrate refreshes the item for the client itself
	}
	// As Desecrate does for its success: refresh the item on the client
	d2r::Engine<void (*)(Game*, Unit*, Unit*, uint32_t, uint32_t, int32_t, uint8_t, uint8_t, uint32_t) noexcept>(SendItem)(
		job->game, job->player, item, 1, 0, At<int32_t>(item, UnitMode), d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemPage)(item),
		d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemStorePage)(item), d2r::Engine<uint32_t (*)(Unit*) noexcept>(UnitPackedXY)(item));
}

// Game thread: `user` is the Kind and the local player handle was captured on the UI thread (see UiStep)
struct Pending {
	Kind kind;
	D2RL::PlayerHandle handle;
};
Pending g_pending {};

void GameStep(const D2RL::PluginContext*, void*) noexcept {
	using namespace d2r::rva;
	const Pending pending = g_pending;
	Game* game = FirstGame();
	Unit* player = d2r::Engine<Unit* (*)(uint32_t) noexcept>(ClientPlayer)(d2r::Engine<uint32_t (*)() noexcept>(LocalPlayerSlot)());
	if (game == nullptr || player == nullptr) {
		plog::Info("test aid: item command: no game or player");
		return;
	}
	D2RL::ItemHandle item = D2RL::InvalidItemHandle;
	if (g_inventory->getCursorItem(g_context, pending.handle, &item) != inv::Result::Success) {
		item = D2RL::InvalidItemHandle;
	}
	if (item == D2RL::InvalidItemHandle) {
		const inv::ItemFilter filter {.structSize = inv::ItemFilterSize, .containerMask = items::ContainerBit(items::ItemContainer::Inventory)};
		g_inventory->forEachInventoryItem(g_context, pending.handle, &filter, FindCellZero, &item);
	}
	if (item == D2RL::InvalidItemHandle) {
		plog::Info("test aid: %s: no item on the cursor and none at inventory (0,0)", pending.kind == Kind::Corrupt ? "corrupt" : "desecrate");
		return;
	}
	Job job {pending.kind, game, player};
	const items::Result edited = g_items->editNativeItem(g_context, item, Act, &job);
	if (edited != items::Result::Success) {
		plog::Info("test aid: the item could not be read (%u)", static_cast<uint32_t>(edited));
	}
}

// UI thread: the SDK names the local player here
void UiStep(const D2RL::PluginContext*, void* user) noexcept {
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (g_inventory->getLocalPlayer(g_context, &player) != inv::Result::Success) {
		plog::Info("test aid: item command: no local player");
		return;
	}
	g_pending = {static_cast<Kind>(reinterpret_cast<uintptr_t>(user)), player};
	if (g_threads->runOnGameThread(g_context, GameStep, nullptr) != D2RL::Threads::Result::Success) {
		plog::Info("test aid: item command: could not queue it on the game thread");
	}
}

}  // namespace

auto QueueItemCommand(const D2RL::PluginContext* context, Kind kind, char* why, size_t whySize) noexcept -> bool {
	g_context = context;
	if (context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize) ||
	    context->QueryService(&g_inventory) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasInventoryServiceField(g_inventory, D2RL::InventoryServiceRequiredSize) ||
	    context->QueryService(&g_items) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasItemServiceField(g_items, D2RL::ItemServiceRequiredSize)) {
		std::snprintf(why, whySize, "the SDK thread, inventory or item service is missing");
		return false;
	}
	if (g_threads->runOnUiThread(context, UiStep, reinterpret_cast<void*>(static_cast<uintptr_t>(kind))) != D2RL::Threads::Result::Success) {
		std::snprintf(why, whySize, "could not queue it on the UI thread");
		return false;
	}
	return true;
}
