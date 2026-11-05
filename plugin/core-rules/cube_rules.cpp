// PD2's corruption in one press (ProjectDiablo.dll 0x366f40; docs/research/ir6-cube-rules.md). A corruption takes two
// rows: a roll row gives the item a hidden `corruptor` value and hands the Worldstone Shard back, then an outcome row
// (op 16 on that value) applies the corruption and takes the shard. PD2 runs the cube again right after a roll row,
// so both fire in one press. D2R runs one row per press; this hook on the executor runs the search and the executor
// once more after a roll row, under PD2's gate (0x364960): the cube holds a Worldstone Shard (`wss ` or `cwss`) and
// none of them is corrupted.
//
// PD2 tags the roll rows with its `ladder` column (2), which the merge cuts. They are the rows whose output carries a
// `corruptnum` mod with a range (1-1000 or 1-3000): exactly PD2's 24 tagged rows, checked against PD2's cubemain.
//
// PD2's Demonic Cube reroll (op 31, 0x3669b0) remakes a unique or set item from its own row and takes one Demonic Cube
// off the stack. On an op-31 row this hook does that through the PluginSDK instead of D2R's executor: one item
// transaction takes one Demonic Cube and the old item and makes the new one in the cube, on the old item's base, row,
// item level, sockets, ethereal and identified state (and quantity, and an ethereal item's durability, as PD2 keeps
// them). D2R's own once-per-game rule is skipped (AllowDuplicateUnique), as PD2's 0x103841d0 does. The executor is the
// hook point only because the SDK has no cube event; the SDK refuses item changes there (Busy), so the transaction is
// queued for the next game update.
#include "cube_rules.h"

#include "d2r.h"
#include "log.h"

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/threads.h>

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t PropCorruptNum = 289;  // merged properties.txt corruptnum (D2R skips the "Expansion" row)
constexpr int32_t StatCorrupted = 413;   // merged itemstatcost corrupted (PD2's 360)
constexpr uint32_t CodeShard = 0x20737377, CodeCubeShard = 0x73737763;  // 'wss ', 'cwss'
constexpr uint8_t OpReroll = 31;
constexpr int OutputMods = 5;

struct SearchResult {
	int32_t row;
	uint8_t found;
};

// The search's out: one {item, int32, int32} entry per output, the executor's last argument
struct MatchOut {
	uint64_t words[6];
};

using ExecutorFn = uint64_t (*)(Game* game, Unit* player, const uint8_t* record, MatchOut* out) noexcept;
using SearchFn = void (*)(SearchResult* result, Game* game, Unit* player, MatchOut* out) noexcept;

ExecutorFn g_executorOriginal = nullptr;
std::atomic<uint32_t> g_rollRows {0};
std::atomic<uint32_t> g_onePress {0};
std::atomic<uint32_t> g_rerolls {0};
const D2RL::PluginContext* g_context = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

auto Inventory(Unit* unit) noexcept -> void* {
	return d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(d2r::rva::UnitInventory)(unit, __FILE__, __LINE__);
}

auto Stat(Unit* unit, int32_t stat) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(unit, stat, 0);
}

auto Code(Unit* item) noexcept -> uint32_t {
	return d2r::Engine<uint32_t (*)(Unit*) noexcept>(d2r::rva::ItemCode)(item);
}

// Calls `visit(item)` for each item in the player's cube until it returns false.
template <class Visit>
auto ForCubeItems(Unit* player, Visit visit) noexcept -> void {
	using namespace d2r::rva;
	void* inventory = Inventory(player);
	if (inventory == nullptr) {
		return;
	}
	const auto next = d2r::Engine<Unit* (*)(Unit*) noexcept>(InventoryNextItem);
	for (Unit* item = d2r::Engine<Unit* (*)(void*) noexcept>(InventoryFirstItem)(inventory); item != nullptr;
	     item = next(item)) {
		if (d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemPage)(item) == CubePageCube && !visit(item)) {
			return;
		}
	}
}

auto IsRollRow(const uint8_t* record) noexcept -> bool {
	using namespace d2r::rva;
	for (int m = 0; m < OutputMods; ++m) {
		const uint8_t* mod = record + CubeOutMods + m * CubeModSize;
		const int32_t prop = At<int32_t>(mod, 0);
		if (prop == -1) {
			return false;
		}
		if (prop == PropCorruptNum && At<int16_t>(mod, 6) != At<int16_t>(mod, 8)) {
			return true;
		}
	}
	return false;
}

// PD2's 0x364960: a Worldstone Shard is in the cube and none in it is corrupted.
auto ShardReady(Unit* player) noexcept -> bool {
	bool shard = false, corrupted = false;
	ForCubeItems(player, [&](Unit* item) noexcept {
		const uint32_t code = Code(item);
		if (code == CodeShard || code == CodeCubeShard) {
			shard = true;
			corrupted = Stat(item, StatCorrupted) != 0;
		}
		return !corrupted;
	});
	return shard && !corrupted;
}

namespace items = D2RL::Items;

struct CubeScan {
	items::ItemInfo target {};
	items::ItemInfo cubes {};
	int targets = 0;
};

auto ScanCube(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto* scan = static_cast<CubeScan*>(user);
	if (item->code == D2RL::Items::MakeItemCode("imrn")) {
		scan->cubes = *item;
	} else if (item->quality == items::Quality::Set || item->quality == items::Quality::Unique) {
		scan->target = *item;
		++scan->targets;
	}
	return D2RL::Inventory::IterationAction::Continue;
}

// Op 31: PD2's 0x3669b0 as one SDK item transaction (top of file), on the game thread.
void RerollNow(const D2RL::PluginContext*, void*) noexcept {
	// shortcut: the local player is the one who pressed in single player; a hosted game's other players need their
	// handle from the native player. Upgrade when hosted cube use matters.
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success) {
		plog::Info("reroll: no local player");
		return;
	}
	CubeScan scan;
	const D2RL::Inventory::ItemFilter filter {.structSize = D2RL::Inventory::ItemFilterSize,
	                                          .containerMask = items::ContainerBit(items::ItemContainer::Cube)};
	g_inventory->forEachInventoryItem(g_context, player, &filter, ScanCube, &scan);
	const items::ItemInfo& old = scan.target;
	if (scan.targets != 1 || scan.cubes.handle == D2RL::InvalidItemHandle) {
		plog::Info("reroll: the cube holds %d unique or set items and %s Demonic Cube", scan.targets,
		           scan.cubes.handle != D2RL::InvalidItemHandle ? "a" : "no");
		return;
	}
	const bool ethereal = (old.stateFlags & items::ItemStateEthereal) != 0;
	const items::TransactionInput inputs[] {
		{.item = old.handle, .quantity = 1, .socketedItemPolicy = items::SocketedItemPolicy::RejectIfNotEmpty},
		{.item = scan.cubes.handle, .quantity = 1, .socketedItemPolicy = items::SocketedItemPolicy::RejectIfNotEmpty}};
	items::ItemCreateSpec spec {};
	spec.structSize = items::ItemCreateSpecSize;
	spec.flags = items::ItemCreateFlagBit(items::ItemCreateFlag::AllowDuplicateUnique);
	spec.code = old.code;
	spec.quality = old.quality;
	spec.qualityRecordId = static_cast<uint32_t>(old.qualityRecordId);
	spec.itemLevel = old.itemLevel;
	spec.seedMode = items::SeedMode::Random;
	spec.quantity = old.quantity > 1 ? static_cast<uint32_t>(old.quantity) : items::DefaultValue;
	spec.durability = ethereal && old.durability > 0 ? static_cast<uint32_t>(old.durability) : items::DefaultValue;
	spec.socketCount = old.socketCount;
	spec.stateFlags = old.stateFlags & items::AllItemStates;
	spec.destination = {.structSize = items::ItemDestinationSize,
	                    .container = items::ItemContainer::Cube,
	                    .placement = items::Placement::Automatic};
	D2RL::ItemHandle made = D2RL::InvalidItemHandle;
	const items::Transaction transaction {.structSize = items::TransactionSize,
	                                      .player = player,
	                                      .inputCount = 2,
	                                      .outputCount = 1,
	                                      .inputs = inputs,
	                                      .outputs = &spec,
	                                      .outputItems = &made,
	                                      .outputCapacity = 1};
	items::TransactionResult result {.structSize = items::TransactionResultSize};
	const items::Result done = g_items->executeTransaction(g_context, &transaction, &result);
	items::ItemInfo now {.structSize = items::ItemInfoSize};
	const bool read = done == items::Result::Success && g_items->getItemInfo(g_context, made, &now) == items::Result::Success;
	plog::Info("reroll: quality %u row %d level %u ethereal %d sockets %u (filled %u), demonic cubes %d -> %d: result %u, "
	           "new row %d (%u so far)",
	           static_cast<uint32_t>(old.quality), old.qualityRecordId, old.itemLevel, ethereal, old.socketCount,
	           old.socketedItemCount, scan.cubes.quantity, done == items::Result::Success ? scan.cubes.quantity - 1 : scan.cubes.quantity,
	           static_cast<uint32_t>(done), read ? now.qualityRecordId : -1, ++g_rerolls);
}

auto Reroll() noexcept -> uint64_t {
	if (g_items == nullptr || g_inventory == nullptr || g_threads == nullptr ||
	    g_threads->runOnGameThread(g_context, RerollNow, nullptr) != D2RL::Threads::Result::Success) {
		plog::Info("reroll: could not queue it");
	}
	return 0;
}

auto CubeExecute(Game* game, Unit* player, const uint8_t* record, MatchOut* out) noexcept -> uint64_t {
	if (game != nullptr && player != nullptr && record != nullptr && record[d2r::rva::CubeOp] == OpReroll) {
		return Reroll();
	}
	const uint64_t made = g_executorOriginal(game, player, record, out);
	if (game == nullptr || player == nullptr || record == nullptr || !IsRollRow(record)) {
		return made;
	}
	++g_rollRows;
	if (!ShardReady(player)) {
		plog::Info("corruption: roll row ran, no clean shard in the cube, no second row");
		return made;
	}
	// Through core-maps' hook, so op 30 blocks still apply to the second search
	SearchResult result {};
	MatchOut again {};
	d2r::Engine<SearchFn>(d2r::rva::CubeRecipeSearch)(&result, game, player, &again);
	if (result.found == 0) {
		plog::Info("corruption: roll row ran, no outcome row matches");
		return made;
	}
	const uint8_t* outcome =
		d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::rva::CubeRecipe)(d2r::DataFlag(game), result.row);
	if (outcome == nullptr) {
		return made;
	}
	plog::Info("corruption: one press runs outcome row %d, %u so far", result.row, ++g_onePress);
	g_executorOriginal(game, player, outcome, &again);
	return made;
}

}  // namespace

auto InstallCubeRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	g_context = context;
	if (context->QueryService(&g_items) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasItemServiceField(g_items, D2RL::ItemServiceRequiredSize)) {
		g_items = nullptr;
	}
	if (context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize)) {
		g_threads = nullptr;
	}
	if (context->QueryService(&g_inventory) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasInventoryServiceField(g_inventory, D2RL::InventoryServiceRequiredSize)) {
		g_inventory = nullptr;
	}
	const bool hook = context->InstallInlineHook(CubeExecutor, CubeExecutorEntry, sizeof(CubeExecutorEntry),
	                                             CubeExecute, &g_executorOriginal);
	std::snprintf(report, reportSize, "corruption in one press, demonic cube reroll%s%s", hook ? "" : " FAILED (bytes differ)",
	              g_items != nullptr && g_inventory != nullptr && g_threads != nullptr ? "" : " (reroll off: a service is missing)");
	return hook ? 1 : 0;
}

auto CubeRuleCount() noexcept -> int {
	return 1;
}

auto CubeRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "corruption roll rows %u, one-press outcomes %u, rerolls %u", g_rollRows.load(),
	              g_onePress.load(), g_rerolls.load());
}
