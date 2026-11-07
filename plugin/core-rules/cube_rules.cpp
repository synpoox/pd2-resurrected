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
// item level, sockets, socketed items, ethereal and identified state, quantity and durability, as PD2 keeps them. D2R's own
// once-per-game rule is skipped (AllowDuplicateUnique), as PD2's 0x103841d0 does. The executor is the hook point only
// because the SDK has no cube event. The SDK refuses item changes inside it (Busy) and names the local player only on
// the UI thread, so the press goes executor -> UI thread (which player) -> next game update (the transaction).
//
// The merge ships the op-31 rows switched off, so D2R never runs them itself; this file switches them on after each
// table load, and only when its hook and the SDK services are in.
#include "cube_rules.h"

#include "d2r.h"
#include "log.h"

#include <D2RLPlugin/game_rules.h>
#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/lifecycle.h>
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
constexpr uint32_t CodeDemonicCube = D2RL::Items::MakeItemCode("imrn");
constexpr uint32_t DataFlags = 4;  // 0x300a90 holds four data-table banks
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
const D2RL::GameRuleService* g_rules = nullptr;
bool g_rerollReady = false;  // the hook and every service the reroll needs are in

// Calls `visit(item)` for each item in the player's cube until it returns false. Native, because it runs inside the
// executor on the pressing player's unit, and the SDK names only the local player, on the UI thread.
template <class Visit>
auto ForCubeItems(Unit* player, Visit visit) noexcept -> void {
	using namespace d2r::rva;
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory)(player, __FILE__, __LINE__);
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
		const uint32_t code = d2r::Engine<uint32_t (*)(Unit*) noexcept>(d2r::rva::ItemCode)(item);
		if (code == CodeShard || code == CodeCubeShard) {
			shard = true;
			corrupted = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(item, StatCorrupted, 0) != 0;
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
	if (item->code == CodeDemonicCube) {
		scan->cubes = *item;
	} else if (item->quality == items::Quality::Set || item->quality == items::Quality::Unique) {
		scan->target = *item;
		++scan->targets;
	}
	return D2RL::Inventory::IterationAction::Continue;
}

// The SDK refuses a quantity on an item that does not stack (InvalidArgument), and ItemInfo gives such an item
// quantity 1, the same as a stack of one.
auto Stacks(const items::ItemInfo& item) noexcept -> bool {
	uint32_t maximum = 0;
	return g_rules->getTotalMaxStack(g_context, item.handle, &maximum) == D2RL::GameRules::Result::Success && maximum > 1;
}

// The old item as a new roll of the same row: everything PD2's 0x3669b0 carries over (top of file).
auto RerollSpec(const items::ItemInfo& old) noexcept -> items::ItemCreateSpec {
	return {.structSize = items::ItemCreateSpecSize,
	        .flags = items::ItemCreateFlagBit(items::ItemCreateFlag::AllowDuplicateUnique),
	        .code = old.code,
	        .quality = old.quality,
	        .qualityRecordId = static_cast<uint32_t>(old.qualityRecordId),
	        .itemLevel = old.itemLevel,
	        .seedMode = items::SeedMode::Random,
	        .quantity = Stacks(old) ? static_cast<uint32_t>(old.quantity) : items::DefaultValue,
	        .durability = old.maximumDurability > 0 ? static_cast<uint32_t>(old.durability) : items::DefaultValue,
	        .socketCount = old.socketCount,
	        .stateFlags = old.stateFlags & items::AllItemStates,
	        .destination = {.structSize = items::ItemDestinationSize,
	                        .container = items::ItemContainer::Cube,
	                        .placement = items::Placement::Automatic}};
}

// The old item's socketed items move to the new one, as PD2's 0x3669b0 moves them. Native: the SDK's socket policies
// only refuse or destroy them. They come out of the old item before the transaction, so it is an empty input, and go
// into the new item after it, or back into the old one if it fails. The item pointers come from editNativeItem.
constexpr int MaxSocketed = 6;

struct Socketed {
	Unit* items[MaxSocketed] {};
	int count = 0;
	bool ok = false;
};

// Takes them out (inventory and stat list); takes none when there are more than MaxSocketed.
void TakeSocketed(const D2RL::PluginContext*, void* native, void* user) noexcept {
	using namespace d2r::rva;
	auto& taken = *static_cast<Socketed*>(user);
	auto* item = static_cast<Unit*>(native);
	if (void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory)(item, __FILE__, __LINE__)) {
		const auto next = d2r::Engine<Unit* (*)(Unit*) noexcept>(InventoryNextItem);
		for (Unit* s = d2r::Engine<Unit* (*)(void*) noexcept>(InventoryFirstItem)(inventory); s != nullptr; s = next(s)) {
			if (taken.count == MaxSocketed) {
				return;
			}
			taken.items[taken.count++] = s;
		}
	}
	for (int i = 0; i < taken.count; ++i) {
		d2r::Engine<void (*)(Unit*, Unit*) noexcept>(StatListUnlink)(nullptr, taken.items[i]);
		d2r::Engine<int32_t (*)(Unit*) noexcept>(InventoryRemove)(taken.items[i]);
	}
	taken.ok = true;
}

Game* g_pressGame = nullptr;  // the executor's game and player, for SendItem (single player: one game at a time)
Unit* g_pressPlayer = nullptr;

void Send(Unit* item, uint32_t itemFlags, uint32_t commandFlags) noexcept {
	using namespace d2r::rva;
	d2r::Engine<void (*)(Game*, Unit*, Unit*, uint32_t, uint32_t, int32_t, uint8_t, uint8_t, uint32_t) noexcept>(SendItem)(
		g_pressGame, g_pressPlayer, item, itemFlags, commandFlags, At<int32_t>(item, UnitMode),
		d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemPage)(item), d2r::Engine<uint8_t (*)(Unit*) noexcept>(ItemStorePage)(item),
		d2r::Engine<uint32_t (*)(Unit*) noexcept>(UnitXY)(item));
}

struct PutBack {
	const Socketed* taken;
	bool send;  // the client saw them leave (the new item); a put-back into the old item tells it nothing
	int placed = 0;
};

// Into the parent's sockets in their old order, as 0x46eb00 places them, without its gems.txt mods (rvas_93847.h).
void PutSocketed(const D2RL::PluginContext*, void* native, void* user) noexcept {
	using namespace d2r::rva;
	auto& put = *static_cast<PutBack*>(user);
	auto* parent = static_cast<Unit*>(native);
	const auto inventoryOf = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(UnitInventory);
	if (inventoryOf(parent, __FILE__, __LINE__) == nullptr) {
		d2r::Engine<void (*)(Unit*) noexcept>(CreateInventory)(parent);
	}
	void* inventory = inventoryOf(parent, __FILE__, __LINE__);
	for (int i = 0; inventory != nullptr && i < put.taken->count; ++i) {
		Unit* s = put.taken->items[i];
		if (d2r::Engine<int32_t (*)(void*, Unit*, int32_t) noexcept>(PlaceInSocket)(inventory, s, 1) == 0) {
			continue;
		}
		d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetObjectMode)(s, ItemModeSocketed);
		d2r::Engine<void (*)(Unit*, uint8_t) noexcept>(SetItemStorePage)(s, 0);
		d2r::Engine<void (*)(Unit*, uint8_t) noexcept>(SetItemPage)(s, 0xff);
		d2r::Engine<void (*)(Unit*, Unit*, int32_t) noexcept>(StatListLink)(parent, s, 1);
		++put.placed;
		if (put.send) {
			Send(s, 0, 2);
		}
	}
	if (put.send) {
		Send(parent, 1, 0);
	}
}

auto Put(D2RL::ItemHandle parent, const Socketed& taken, bool send) noexcept -> int {
	PutBack put {.taken = &taken, .send = send};
	if (taken.count > 0 && g_items->editNativeItem(g_context, parent, PutSocketed, &put) != items::Result::Success) {
		return -1;
	}
	return put.placed;
}

// Op 31 on the game thread: one SDK item transaction for the player the UI thread named.
void RerollNow(const D2RL::PluginContext*, void* user) noexcept {
	const auto player = reinterpret_cast<D2RL::PlayerHandle>(user);
	CubeScan scan;
	const D2RL::Inventory::ItemFilter filter {.structSize = D2RL::Inventory::ItemFilterSize,
	                                          .containerMask = items::ContainerBit(items::ItemContainer::Cube)};
	if (const auto listed = g_inventory->forEachInventoryItem(g_context, player, &filter, ScanCube, &scan);
	    listed != D2RL::Inventory::Result::Success) {
		plog::Info("reroll: could not read the cube (%u)", static_cast<uint32_t>(listed));
		return;
	}
	const items::ItemInfo& old = scan.target;
	if (scan.targets != 1 || scan.cubes.handle == D2RL::InvalidItemHandle) {
		plog::Info("reroll: the cube holds %d unique or set items and %s Demonic Cube", scan.targets,
		           scan.cubes.handle != D2RL::InvalidItemHandle ? "a" : "no");
		return;
	}
	Socketed taken;
	if (g_items->editNativeItem(g_context, old.handle, TakeSocketed, &taken) != items::Result::Success || !taken.ok) {
		plog::Info("reroll: refused, could not take out the socketed items (more than %d?)", MaxSocketed);
		return;
	}
	const items::TransactionInput inputs[] {
		{.item = old.handle, .quantity = 1, .socketedItemPolicy = items::SocketedItemPolicy::RejectIfNotEmpty},
		{.item = scan.cubes.handle, .quantity = 1, .socketedItemPolicy = items::SocketedItemPolicy::RejectIfNotEmpty}};
	const items::ItemCreateSpec spec = RerollSpec(old);
	D2RL::ItemHandle newItem = D2RL::InvalidItemHandle;
	const items::Transaction transaction {.structSize = items::TransactionSize,
	                                      .player = player,
	                                      .inputCount = 2,
	                                      .outputCount = 1,
	                                      .inputs = inputs,
	                                      .outputs = &spec,
	                                      .outputItems = &newItem,
	                                      .outputCapacity = 1};
	items::TransactionResult result {.structSize = items::TransactionResultSize};
	const items::Result status = g_items->executeTransaction(g_context, &transaction, &result);
	// Into the new item, or back into the old one, which the failed transaction left as it was.
	// shortcut: one that PlaceInSocket refuses stays out of any inventory (the log's placed count shows it); unlikely,
	// since the new item has the old one's socket count. Upgrade if a log ever shows fewer placed than taken.
	const int placed = Put(status == items::Result::Success ? newItem : old.handle, taken, status == items::Result::Success);
	items::ItemInfo remade {.structSize = items::ItemInfoSize};
	const bool remadeRead =
		status == items::Result::Success && g_items->getItemInfo(g_context, newItem, &remade) == items::Result::Success;
	plog::Info("reroll: quality %u row %d level %u quantity %d durability %d/%d sockets %u (filled %d), demonic cubes %d: "
	           "result %u, new row %d quantity %d durability %d/%d, %d of them placed (%u so far)",
	           static_cast<uint32_t>(old.quality), old.qualityRecordId, old.itemLevel, old.quantity, old.durability,
	           old.maximumDurability, old.socketCount, taken.count, scan.cubes.quantity, static_cast<uint32_t>(status),
	           remadeRead ? remade.qualityRecordId : -1, remadeRead ? remade.quantity : -1,
	           remadeRead ? remade.durability : -1, remadeRead ? remade.maximumDurability : -1, placed, ++g_rerolls);
}

// UI thread: the SDK names the local player only here. The pressing player's unit id comes from the executor; a
// native read of the client's own player checks they are the same, since the SDK cannot name the player who pressed.
// shortcut: refuses another player's press in a hosted game (the host has no SDK handle for them without a plugin
// message from their client), upgrade when hosted cube use matters.
void PickPlayer(const D2RL::PluginContext*, void* user) noexcept {
	using namespace d2r::rva;
	const auto pressedBy = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(user));
	Unit* local = d2r::Engine<Unit* (*)(uint32_t) noexcept>(ClientPlayer)(d2r::Engine<uint32_t (*)() noexcept>(LocalPlayerSlot)());
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (local == nullptr || At<uint32_t>(local, UnitIdField) != pressedBy) {
		plog::Info("reroll: refused, pressed by player %u, not this game's own player", pressedBy);
	} else if (g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success) {
		plog::Info("reroll: no local player");
	} else if (g_threads->runOnGameThread(g_context, RerollNow, reinterpret_cast<void*>(player)) !=
	           D2RL::Threads::Result::Success) {
		plog::Info("reroll: could not queue it");
	}
}

// Executor, game thread: never D2R's own row (top of file), so nothing is taken unless the SDK transaction runs.
auto Reroll(Game* game, Unit* player) noexcept -> uint64_t {
	const uint32_t id = At<uint32_t>(player, d2r::rva::UnitIdField);
	g_pressGame = game;
	g_pressPlayer = player;
	if (!g_rerollReady ||
	    g_threads->runOnUiThread(g_context, PickPlayer, reinterpret_cast<void*>(static_cast<uintptr_t>(id))) !=
	        D2RL::Threads::Result::Success) {
		plog::Info("reroll: could not queue it");
	}
	return 0;
}

// Switches the op-31 rows on after each table load (top of file). Native: the SDK's table views are read-only.
void __cdecl EnableRerollRows(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	using namespace d2r::rva;
	int enabled = 0;
	for (uint32_t flag = 0; flag < DataFlags; ++flag) {
		if (d2r::Engine<void* (*)(uint32_t) noexcept>(DataTables)(flag) == nullptr) {
			continue;
		}
		const int32_t rows = d2r::Engine<int32_t (*)(uint32_t) noexcept>(CubeRecipeCount)(flag);
		for (int32_t i = 0; i < rows; ++i) {
			auto* row = const_cast<uint8_t*>(d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(CubeRecipe)(flag, i));
			if (row != nullptr && row[CubeOp] == OpReroll) {
				row[CubeEnabled] = 1;
				++enabled;
			}
		}
	}
	plog::Info("reroll: %d demonic cube rows switched on", enabled);
}

template <class Service>
auto Query(const D2RL::PluginContext* context, const Service*& service, bool (*has)(const Service*, uint32_t),
           uint32_t required) noexcept -> bool {
	if (context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !has(service, required)) {
		service = nullptr;
	}
	return service != nullptr;
}

auto CubeExecute(Game* game, Unit* player, const uint8_t* record, MatchOut* out) noexcept -> uint64_t {
	if (game != nullptr && player != nullptr && record != nullptr && record[d2r::rva::CubeOp] == OpReroll) {
		return Reroll(game, player);
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
	const D2RL::LifecycleService* lifecycle = nullptr;
	const bool services = Query(context, g_items, D2RL::HasItemServiceField, D2RL::ItemServiceRequiredSize) &
	                      Query(context, g_threads, D2RL::HasThreadServiceField, D2RL::ThreadServiceRequiredSize) &
	                      Query(context, g_inventory, D2RL::HasInventoryServiceField, D2RL::InventoryServiceRequiredSize) &
	                      Query(context, g_rules, D2RL::HasGameRuleServiceField, D2RL::GameRuleServiceRequiredSize) &
	                      Query(context, lifecycle, D2RL::HasLifecycleServiceField, D2RL::LifecycleServiceRequiredSize);
	const bool hook = context->InstallInlineHook(CubeExecutor, CubeExecutorEntry, sizeof(CubeExecutorEntry),
	                                             CubeExecute, &g_executorOriginal);
	const D2RL::Lifecycle::DataTablesLoadedListener listener {.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
	                                                          .callback = EnableRerollRows};
	D2RL::Lifecycle::ListenerHandle listening = D2RL::Lifecycle::InvalidHandle;
	g_rerollReady = hook && services &&
	                lifecycle->registerDataTablesLoadedListener(context, &listener, &listening) == D2RL::Lifecycle::Result::Success;
	std::snprintf(report, reportSize, "corruption in one press, demonic cube reroll%s%s", hook ? "" : " FAILED (bytes differ)",
	              g_rerollReady ? "" : " (reroll off: its rows stay off; a service or the table listener is missing)");
	return hook ? 1 : 0;
}

auto CubeRuleCount() noexcept -> int {
	return 1;
}

auto CubeRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "corruption roll rows %u, one-press outcomes %u, rerolls %u", g_rollRows.load(),
	              g_onePress.load(), g_rerolls.load());
}
