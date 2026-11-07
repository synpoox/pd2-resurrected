// PD2's stack-aware cube matching (ProjectDiablo.dll 0x364e30, #315). PD2 matches each input with its qty set to 1,
// then takes the quantity (stat 70) of every stack it matched, one per other item, off the input's qty: the input
// matches once nothing is left, so a stack of 3 El runes is 3 El. The row then matches only if every item in the cube
// was used by an input. Its executor (0x3653d0) takes what the row needs off the stacks, so a stack of 5 El leaves 2.
//
// D2R counts each item as one: its search wants numinputs == items in the cube (0x529090) and its matcher (0x529d50)
// counts matching items toward qty. The merge sets numinputs to the sum of the inputs' qty (tools/pd2_items.py). Here:
// - the matcher hook calls D2R's own matcher on a copy of the row with the input's qty out of reach (255), so it flags
//   every cube item that input matches, as it does on a match, and returns PD2's test on their summed quantity;
// - the search's numinputs filter is patched out; the hook does it on input 0 as "no more items than numinputs" (a
//   stack holds several), and on the last input checks that every cube item was used, as PD2 does;
// - D2R's executor takes every cube item whole, so before it runs, the units a row leaves are queued to come back to
//   the cube through the SDK (createItem) once the stack is gone.
// Native: the SDK has no cube-recipe service, and the matcher and search run inside D2R's cube packet handler.
#include "cube_stacks.h"

#include "log.h"

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/mutations.h>
#include <D2RLPlugin/threads.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;
namespace items = D2RL::Items;

constexpr uint8_t OpBlock = 30;  // core-maps: matched without the input count, as PD2 does
constexpr int Inputs = 7;
constexpr int Slots = d2r::rva::CubeMatchUsedSlots;

struct MatchOut {
	uint64_t words[6];
};

using MatchFn = uint64_t (*)(Game* game, void* inventory, const uint8_t* row, int32_t input, MatchOut* out,
                             int32_t* used) noexcept;

MatchFn g_matchOriginal = nullptr;
std::atomic<uint32_t> g_stackMatches {0};
std::atomic<uint32_t> g_leftovers {0};
const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

// The cube items in the order the matcher numbers them (used[k])
struct CubeItems {
	Unit* item[Slots] = {};
	int count = 0;  // all of them, also past Slots
};

auto ReadCube(void* inventory) noexcept -> CubeItems {
	using namespace d2r::rva;
	CubeItems cube;
	const auto next = d2r::Engine<Unit* (*)(Unit*) noexcept>(InventoryNextItem);
	for (Unit* item = d2r::Engine<Unit* (*)(void*) noexcept>(InventoryFirstItem)(inventory); item != nullptr;
	     item = next(item)) {
		if (d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemPage)(item) == CubePageCube) {
			if (cube.count < Slots) {
				cube.item[cube.count] = item;
			}
			++cube.count;
		}
	}
	return cube;
}

// shortcut: an empty tome (quantity 0) counts as one, PD2 as none; upgrade if a recipe ever takes tomes
auto Quantity(Unit* item) noexcept -> int32_t {
	const int32_t quantity =
		d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(item, d2r::rva::StatQuantity, 0);
	return quantity > 0 ? quantity : 1;
}

auto Needed(const uint8_t* row, int input) noexcept -> int32_t {
	const uint8_t qty = row[d2r::rva::CubeInputs + input * d2r::rva::CubeInputSize + d2r::rva::CubeInputQty];
	return qty != 0 ? qty : 1;
}

// D2R's matcher on input `input` with its qty out of reach: flags every cube item the input matches in `used` and fills
// `out` as a match does. `taken` gets the newly flagged slots.
auto MarkInput(Game* game, void* inventory, const uint8_t* row, int input, MatchOut* out, int32_t* used,
               bool* taken) noexcept -> void {
	int32_t before[Slots];
	std::memcpy(before, used, sizeof(before));
	uint8_t probe[d2r::rva::CubeRecordSize];
	std::memcpy(probe, row, sizeof(probe));
	probe[d2r::rva::CubeInputs + input * d2r::rva::CubeInputSize + d2r::rva::CubeInputQty] = 0xff;
	g_matchOriginal(game, inventory, probe, input, out, used);
	for (int k = 0; k < Slots; ++k) {
		taken[k] = used[k] != 0 && before[k] == 0;
	}
}

auto InputMatch(Game* game, void* inventory, const uint8_t* row, int32_t input, MatchOut* out, int32_t* used) noexcept
	-> uint64_t {
	using namespace d2r::rva;
	if (inventory == nullptr || row == nullptr || used == nullptr || out == nullptr || input < 0 || input >= Inputs) {
		return g_matchOriginal(game, inventory, row, input, out, used);
	}
	const CubeItems cube = ReadCube(inventory);
	if (input == 0 && row[CubeOp] != OpBlock && cube.count > row[CubeNumInputs]) {
		return 0;  // the search's numinputs filter, patched out there (top of file)
	}
	if ((At<uint16_t>(row, CubeInputs + input * CubeInputSize) & 3) == 0) {
		if (g_matchOriginal(game, inventory, row, input, out, used) == 0) {
			return 0;
		}
	} else {
		bool taken[Slots];
		MarkInput(game, inventory, row, input, out, used, taken);
		int32_t sum = 0, matched = 0;
		for (int k = 0; k < Slots && k < cube.count; ++k) {
			if (taken[k]) {
				sum += Quantity(cube.item[k]);
				++matched;
			}
		}
		if (sum < Needed(row, input)) {
			return 0;
		}
		if (sum > matched) {
			++g_stackMatches;
		}
	}
	if (input == Inputs - 1) {
		for (int k = 0; k < cube.count; ++k) {
			if (k >= Slots || used[k] == 0) {
				return 0;  // an item no input used
			}
		}
	}
	return 1;
}

// --- Leftovers ------------------------------------------------------------------------------------------------------

struct Leftover {
	uint32_t code;
	uint32_t quantity;
	uint32_t itemLevel;
	uint32_t sourceId;  // the stack's unit id: the executor must have taken it before it comes back
};

Leftover g_pending[Slots];
int g_pendingCount = 0;
std::atomic<bool> g_busy {false};  // a batch is queued; game thread sets it, the game-thread callback clears it

struct SourceScan {
	bool left[Slots] = {};  // that leftover's stack is still in the cube
};

auto ScanSources(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept
	-> D2RL::Inventory::IterationAction {
	auto* scan = static_cast<SourceScan*>(user);
	for (int i = 0; i < g_pendingCount; ++i) {
		scan->left[i] |= g_pending[i].sourceId == item->runtimeId;
	}
	return D2RL::Inventory::IterationAction::Continue;
}

auto Create(D2RL::PlayerHandle player, const Leftover& left, items::ItemContainer container) noexcept -> items::Result {
	const items::ItemCreateSpec spec {.structSize = items::ItemCreateSpecSize,
	                                  .code = left.code,
	                                  .quality = items::Quality::Normal,
	                                  .qualityRecordId = items::RandomQualityRecord,  // 0 is refused (InvalidArgument)
	                                  .itemLevel = left.itemLevel,
	                                  .seedMode = items::SeedMode::Random,
	                                  .quantity = left.quantity,
	                                  .durability = items::DefaultValue,
	                                  .stateFlags = items::ItemStateIdentified,  // else the stack comes back unidentified
	                                  .destination = {.structSize = items::ItemDestinationSize,
	                                                  .container = container,
	                                                  .placement = items::Placement::Automatic}};
	D2RL::ItemHandle made = D2RL::InvalidItemHandle;
	return g_items->createItem(g_context, player, &spec, &made);
}

// Game thread, after the executor: the leftovers back in the cube (the ground when it is full)
void MakeLeftovers(const D2RL::PluginContext*, void* user) noexcept {
	const auto player = reinterpret_cast<D2RL::PlayerHandle>(user);
	SourceScan scan;
	const D2RL::Inventory::ItemFilter filter {.structSize = D2RL::Inventory::ItemFilterSize,
	                                          .containerMask = items::ContainerBit(items::ItemContainer::Cube)};
	const auto listed = g_inventory->forEachInventoryItem(g_context, player, &filter, ScanSources, &scan);
	for (int i = 0; i < g_pendingCount; ++i) {
		const Leftover& left = g_pending[i];
		// A stack the row kept (useitem: the rune upgrades' key) stays whole; an unread cube makes nothing
		if (listed != D2RL::Inventory::Result::Success || scan.left[i]) {
			plog::Info("stacks: leftover %.4s x%u not made: its stack is still in the cube (read %u)",
			           reinterpret_cast<const char*>(&left.code), left.quantity, static_cast<uint32_t>(listed));
			continue;
		}
		items::Result made = Create(player, left, items::ItemContainer::Cube);
		if (made != items::Result::Success) {
			made = Create(player, left, items::ItemContainer::Ground);
		}
		plog::Info("stacks: leftover %.4s x%u back: result %u (%u so far)", reinterpret_cast<const char*>(&left.code),
		           left.quantity, static_cast<uint32_t>(made), ++g_leftovers);
	}
	g_pendingCount = 0;
	g_busy.store(false, std::memory_order_release);
}

void DropBatch(const char* why) noexcept {
	plog::Info("stacks: %s; %d leftovers not made", why, g_pendingCount);
	g_pendingCount = 0;
	g_busy.store(false, std::memory_order_release);
}

// UI thread: the SDK names the local player only here (as core-rules' reroll, cube_rules.cpp)
void PickPlayer(const D2RL::PluginContext*, void* user) noexcept {
	using namespace d2r::rva;
	const auto pressedBy = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(user));
	Unit* local = d2r::Engine<Unit* (*)(uint32_t) noexcept>(ClientPlayer)(d2r::Engine<uint32_t (*)() noexcept>(LocalPlayerSlot)());
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (local == nullptr || At<uint32_t>(local, UnitIdField) != pressedBy) {
		DropBatch("pressed by another player");
	} else if (g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success) {
		DropBatch("no local player");
	} else if (g_threads->runOnGameThread(g_context, MakeLeftovers, reinterpret_cast<void*>(player)) !=
	           D2RL::Threads::Result::Success) {
		DropBatch("could not queue them");
	}
}

template <class Service>
auto Query(const D2RL::PluginContext* context, const Service*& service, bool (*has)(const Service*, uint32_t),
           uint32_t required) noexcept -> bool {
	if (context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !has(service, required)) {
		service = nullptr;
	}
	return service != nullptr;
}

auto PatchNumInputs(const D2RL::PluginContext* context) noexcept -> bool {
	using namespace d2r::rva;
	const D2RL::MutationService* mutations = nullptr;
	if (!Query(context, mutations, D2RL::HasMutationServiceField, D2RL::MutationServiceRequiredSize)) {
		return false;
	}
	D2RL::Mutations::TransactionHandle transaction = 0;
	if (mutations->beginTransaction(context, &transaction) != D2RL::Mutations::Result::Success) {
		return false;
	}
	const D2RL::Mutations::BytePatchRequest patch {.structSize = D2RL::Mutations::BytePatchRequestSize,
	                                               .rva = CubeNumInputsJump,
	                                               .expected = CubeNumInputsJumpD2R,
	                                               .expectedSize = sizeof(CubeNumInputsJumpD2R),
	                                               .bytes = CubeNumInputsJumpOff,
	                                               .size = sizeof(CubeNumInputsJumpOff)};
	D2RL::Mutations::OperationHandle operation = 0;
	D2RL::Mutations::CommitResult result {.structSize = D2RL::Mutations::CommitResultSize};
	if (mutations->stageBytePatch(context, transaction, &patch, &operation) != D2RL::Mutations::Result::Success ||
	    mutations->commit(context, transaction, &result) != D2RL::Mutations::Result::Success) {
		mutations->cancelTransaction(context, transaction);
		return false;
	}
	return true;
}

bool g_ready = false;  // the hook, the patch and the leftover services are in

}  // namespace

auto KeepCubeLeftovers(Game* game, Unit* player, const uint8_t* record) noexcept -> bool {
	using namespace d2r::rva;
	if (!g_ready || game == nullptr || player == nullptr || record == nullptr) {
		return true;
	}
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory)(player, __FILE__, __LINE__);
	if (inventory == nullptr) {
		return true;
	}
	// The search's flags again: same matcher, same order, a fresh used per row
	const CubeItems cube = ReadCube(inventory);
	Leftover found[Slots];
	int count = 0;
	MatchOut out {};
	int32_t used[Slots] = {};
	for (int input = 0; input < Inputs; ++input) {
		if ((At<uint16_t>(record, CubeInputs + input * CubeInputSize) & 3) == 0) {
			continue;
		}
		bool taken[Slots];
		MarkInput(game, inventory, record, input, &out, used, taken);
		int32_t need = Needed(record, input);
		for (int k = 0; k < Slots && k < cube.count; ++k) {
			if (!taken[k]) {
				continue;
			}
			Unit* item = cube.item[k];
			const int32_t quantity = Quantity(item);
			const int32_t use = quantity < need ? quantity : need;
			need -= use;
			if (use == quantity) {
				continue;
			}
			if (d2r::Engine<int32_t (*)(Unit*) noexcept>(ItemQuality)(item) != QualityNormal) {
				plog::Info("stacks: refused, a stack the row leaves units of is not normal quality");
				return false;  // createItem would remake it without its affixes
			}
			found[count++] = {d2r::Engine<uint32_t (*)(Unit*) noexcept>(ItemCode)(item), static_cast<uint32_t>(quantity - use),
			                  d2r::Engine<uint32_t (*)(Unit*) noexcept>(ItemLevel)(item), At<uint32_t>(item, UnitIdField)};
		}
	}
	if (count == 0) {
		return true;
	}
	if (g_busy.exchange(true, std::memory_order_acquire)) {
		plog::Info("stacks: refused, the last press's leftovers are not back yet");
		return false;
	}
	std::memcpy(g_pending, found, sizeof(Leftover) * count);
	g_pendingCount = count;
	const uint32_t id = At<uint32_t>(player, UnitIdField);
	if (g_threads->runOnUiThread(g_context, PickPlayer, reinterpret_cast<void*>(static_cast<uintptr_t>(id))) !=
	    D2RL::Threads::Result::Success) {
		DropBatch("could not queue them; transmute refused");
		return false;
	}
	return true;
}

auto InstallCubeStacks(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	g_context = context;
	const bool services = Query(context, g_items, D2RL::HasItemServiceField, D2RL::ItemServiceRequiredSize) &
	                      Query(context, g_threads, D2RL::HasThreadServiceField, D2RL::ThreadServiceRequiredSize) &
	                      Query(context, g_inventory, D2RL::HasInventoryServiceField, D2RL::InventoryServiceRequiredSize);
	// The hook first: the patch alone would match rows with items to spare, and the executor takes every cube item
	const bool hook = services && context->InstallInlineHook(CubeInputMatch, CubeInputMatchEntry, sizeof(CubeInputMatchEntry),
	                                                         InputMatch, &g_matchOriginal);
	const bool patch = hook && PatchNumInputs(context);
	g_ready = patch;
	std::snprintf(report, reportSize, "stacks count as their quantity%s",
	              g_ready ? "" : (!services ? " FAILED (an SDK service is missing)" : " FAILED (bytes differ)"));
	return g_ready ? 1 : 0;
}

auto CubeStackCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "stack matches %u, leftovers back %u", g_stackMatches.load(), g_leftovers.load());
}
