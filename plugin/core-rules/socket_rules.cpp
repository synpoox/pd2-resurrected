// Socketed gems and runes apply their gems.txt mods once (#450).
//
// Our merged misc.txt gives every gem and rune compactsave=0 (PD2's stacks need it). D2R then keeps the filler's own
// stat list as well as applying gems.txt, so the mods count twice on insertion and a third time after a reload.
// A socketed filler is always one item and never comes out, so after its mods are applied it gets the item flag
// compact (0x200000): the save and the item packet then write it without stats, as vanilla's compactsave=1 does.
// The SDK has no service for item flags or this function, hence the native hook (pd2_engine.h).
//
// Throwing weapons and quivers roll sockets (#1828). D2R's socket roll (93847: 0x441fc0) returns when the stackable
// test 0x374660 says yes (call at 0x441ffc). PD2 (ProjectDiablo.dll 0x10374be0, over 1.13c D2Game 0xec90) runs the roll
// when the item is not stackable or its own items.txt type (D2Common #11088, an exact compare) is one of six: 1.13c
// 42, 43, 44, 87, 5, 6. That one call is redirected (RedirectCall) to StackableNoSockets: no SDK service reaches the
// socket roll.
//
// A stacked gem or rune goes into a socket one at a time (#2119). D2R's insert (93847: 0x46eb00, ItemMode.cpp; (game,
// player, filler id, parent id, flag)) puts the whole filler unit in the socket: D2R's gems and runes never stack, PD2's
// do. The hook sets the filler's quantity (stat 70) to 1 before the insert and, when it went in, makes the rest again on
// the cursor through the SDK (createItem; the inventory, then the ground, when that fails). No SDK service sees socketing.
#include "socket_rules.h"

#include "item_rules.h"
#include "pd2_engine.h"
#include "log.h"
#include "sig_hook.h"

#include <D2RLPlugin/data_tables.h>
#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/threads.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <mutex>

namespace {

using d2r::At;
using d2r::Unit;

std::atomic<uint32_t> g_marked {0};

using SocketModsFn = void (*)(uint8_t dataFlag, Unit* filler, Unit* parent, int32_t arg) noexcept;
SocketModsFn g_original = nullptr;

auto IsItem(Unit* unit) noexcept -> bool {
	return unit != nullptr && At<int32_t>(unit, d2r::rva::UnitType) == 4;
}

void SocketMods(uint8_t dataFlag, Unit* filler, Unit* parent, int32_t arg) noexcept {
	g_original(dataFlag, filler, parent, arg);
	// Only an item parent: a jewel (its own rolled stats) never matches the type test.
	if (IsItem(parent)) {
		MarkSocketedCompact(filler);
	}
}

// mov rcx,rdi / call socketable 0x373610 / test / jz / mov rcx,rdi / call stackable 0x374660 (+28) / test / jnz /
// mov rcx,rdi / call max sockets 0x36ead0 / test al,al / jz, after the item quality >= 2 test
constexpr const char* StackableCallPattern =
    "83 f8 02 0f 8c ?? ?? ?? ?? 48 8b cf e8 ?? ?? ?? ?? 85 c0 0f 84 ?? ?? ?? ?? 48 8b cf e8 ?? ?? ?? ?? 85 c0 0f 85 ?? ?? ?? ?? "
    "48 8b cf e8 ?? ?? ?? ?? 84 c0 0f 84";
constexpr uint32_t StackableCallAt = 28;
// itemtypes codes of PD2's six 1.13c types: tkni 42, taxe 43, jave 44, ajav 87, bowq 5, xboq 6
constexpr uint32_t SocketTypes[] = {D2RL::DataTables::MakeFourCC('t', 'k', 'n', 'i'), D2RL::DataTables::MakeFourCC('t', 'a', 'x', 'e'),
                                    D2RL::DataTables::MakeFourCC('j', 'a', 'v', 'e'), D2RL::DataTables::MakeFourCC('a', 'j', 'a', 'v'),
                                    D2RL::DataTables::MakeFourCC('b', 'o', 'w', 'q'), D2RL::DataTables::MakeFourCC('x', 'b', 'o', 'q')};
constexpr int Banks = 4;  // DataTables::Bank 1-3, indexed by the item's data flag

using StackableFn = int32_t (*)(Unit* item) noexcept;
using ItemRecordFn = const uint8_t* (*)(uint32_t dataFlag, int32_t classId) noexcept;
StackableFn g_stackable = nullptr;
const D2RL::PluginContext* g_context = nullptr;
const D2RL::DataTableService* g_tables = nullptr;
int32_t g_typeRows[Banks][std::size(SocketTypes)] = {};  // row + 1, 0 = not looked up yet, -1 = not in the bank (game thread only)
std::atomic<uint32_t> g_stackRolls {0};

// The itemtypes row of SocketTypes[i] in the bank (-1: not there), looked up on first use; only Busy (off the game
// thread) is asked again
auto TypeRow(uint8_t bank, size_t i) noexcept -> int32_t {
	int32_t& row = g_typeRows[bank][i];
	if (row == 0) {
		D2RL::DataTables::RowView view {.structSize = D2RL::DataTables::RowViewSize};
		const auto result = g_tables->findRowByCode(g_context, static_cast<D2RL::DataTables::Bank>(bank),
		                                            D2RL::DataTables::TableId::ItemTypes, SocketTypes[i], &view);
		if (result == D2RL::DataTables::Result::Busy) {
			return -1;
		}
		row = result == D2RL::DataTables::Result::Success ? static_cast<int32_t>(view.rowIndex) + 1 : -1;
	}
	return row < 0 ? -1 : row - 1;
}

// D2R's stackable answer, but 0 (not stackable: the socket roll goes on) for an item whose own type is one of the six
auto StackableNoSockets(Unit* item) noexcept -> int32_t {
	const int32_t stackable = g_stackable(item);
	if (stackable == 0) {
		return 0;
	}
	const uint8_t bank = d2r::Engine<uint8_t (*)(Unit*) noexcept>(d2r::sig::UnitDataFlag)(item);
	const uint8_t* record = bank < Banks ? d2r::Engine<ItemRecordFn>(d2r::sig::ItemRecord)(bank, At<int32_t>(item, d2r::rva::UnitClassId))
	                                     : nullptr;
	if (record == nullptr) {
		return stackable;
	}
	const int16_t type = At<int16_t>(record, d2r::rva::ItType);
	for (size_t i = 0; i < std::size(SocketTypes); ++i) {
		if (TypeRow(bank, i) == type) {
			++g_stackRolls;
			return 0;
		}
	}
	return stackable;
}

auto InstallStackableSockets(const D2RL::PluginContext* context) noexcept -> bool {
	if (d2r::sig::UnitDataFlag.state != sigscan::State::Ok || d2r::sig::ItemRecord.state != sigscan::State::Ok ||
	    context->QueryService(&g_tables) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasDataTableServiceField(g_tables, D2RL::DataTableServiceRequiredSize)) {
		return false;
	}
	g_context = context;
	return RedirectCall(context, StackableCallPattern, StackableCallAt, reinterpret_cast<uint64_t>(&StackableNoSockets),
	                    reinterpret_cast<uint64_t*>(&g_stackable));
}

// push rbx..r14 / sub rsp,0x70 / cookie / mov r14,rdx / mov ebx,r9d / mov edx,4 / mov rbp,rcx / call FindUnit
constexpr const char* InsertPattern =
    "40 53 55 56 57 41 56 48 83 ec 70 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 44 24 50 4c 8b f2 41 8b d9 ba 04 00 00 00 48 8b e9 e8";
constexpr uint8_t InsertEntry[] = {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56};
// returns 1 when placed, 0 when refused: the packet handler (0x4fa29c) tests it and runs its put-back path on 0
using InsertFn = int32_t (*)(void* game, Unit* player, uint32_t fillerId, uint32_t parentId, uint8_t flag) noexcept;
InsertFn g_insert = nullptr;
constexpr int32_t ItemModeCursor = 4, ItemModeSocketed = 6;
std::atomic<uint32_t> g_split {0}, g_refusedRemote {0};

// The rest of a socketed stack, made again on the game thread. shortcut: the local player only (the SDK names the player
// on the UI thread, as stash_stacks.cpp); a remote player's stacked insert is refused, so nothing is lost; upgrade for a
// hosted game. Leaving the game before the queued job runs (one update) loses the rest.
struct Rest {
	uint32_t code, quantity, itemLevel;
};
constexpr int MaxRests = 16;
Rest g_rests[MaxRests];
int g_restCount = 0;
std::mutex g_restLock;
const D2RL::ItemService* g_items = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
// kept from LocalPlayerReady, so the rest is made on the next game update with no UI-thread hop first
std::atomic<D2RL::PlayerHandle> g_localPlayer {D2RL::InvalidPlayerHandle};

void KeepLocalPlayer(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	g_localPlayer = g_inventory->getLocalPlayer(context, &player) == D2RL::Inventory::Result::Success ? player
	                                                                                                   : D2RL::InvalidPlayerHandle;
}

// server and client unit ids match, in a local or a hosted game
auto LocalPlayerId() noexcept -> uint32_t {
	using namespace d2r::rva;
	Unit* local = d2r::Engine<Unit* (*)(uint32_t) noexcept>(ClientPlayer)(d2r::Engine<uint32_t (*)() noexcept>(LocalPlayerSlot)());
	return local != nullptr ? At<uint32_t>(local, UnitIdField) : 0xffffffffu;
}

void PickPlayer(const D2RL::PluginContext*, void*) noexcept;

// Game thread: each rest to the cursor, else the inventory, else the ground
void MakeRests(const D2RL::PluginContext*, void* user) noexcept {
	namespace items = D2RL::Items;
	const auto player = reinterpret_cast<D2RL::PlayerHandle>(user);
	Rest rests[MaxRests];
	int count = 0;
	{
		std::lock_guard<std::mutex> lock(g_restLock);
		std::memcpy(rests, g_rests, sizeof(Rest) * g_restCount);
		count = g_restCount;
		g_restCount = 0;
	}
	for (int i = 0; i < count; ++i) {
		items::Result made = items::Result::Success;
		for (const auto container : {items::ItemContainer::Cursor, items::ItemContainer::Inventory, items::ItemContainer::Ground}) {
			const items::ItemCreateSpec spec {.structSize = items::ItemCreateSpecSize,
			                                  .code = rests[i].code,
			                                  .quality = items::Quality::Normal,
			                                  .qualityRecordId = items::RandomQualityRecord,  // 0 is refused (InvalidArgument)
			                                  .itemLevel = rests[i].itemLevel > 0 ? rests[i].itemLevel : 1,
			                                  .seedMode = items::SeedMode::Random,
			                                  // a single keeps the native default (the stat may be absent on one)
			                                  .quantity = rests[i].quantity > 1 ? rests[i].quantity : items::DefaultValue,
			                                  .durability = items::DefaultValue,
			                                  .stateFlags = items::ItemStateIdentified,
			                                  .destination = {.structSize = items::ItemDestinationSize,
			                                                  .container = container,
			                                                  .placement = items::Placement::Automatic}};
			D2RL::ItemHandle item = D2RL::InvalidItemHandle;
			made = g_items->createItem(g_context, player, &spec, &item);
			if (made == items::Result::Success || made == items::Result::StaleHandle) {
				break;
			}
		}
		if (made == items::Result::StaleHandle && player == g_localPlayer.exchange(D2RL::InvalidPlayerHandle)) {
			// the kept handle is from an earlier game: these rests go back through the UI thread for a fresh one
			bool queued = false;
			{
				std::lock_guard<std::mutex> lock(g_restLock);
				for (int k = i; k < count && g_restCount < MaxRests; ++k) {
					g_rests[g_restCount++] = rests[k];
				}
				queued = g_threads->runOnUiThread(g_context, PickPlayer, nullptr) == D2RL::Threads::Result::Success;
				if (!queued) {
					g_restCount = 0;
				}
			}
			if (!queued) {
				plog::Warn("sockets: could not queue the rest of a stack; it is lost");
			}
			return;
		}
		if (made != items::Result::Success) {
			plog::Warn("sockets: the rest of a stack, %.4s x%u (ilvl %u), not made again: result %u",
			           reinterpret_cast<const char*>(&rests[i].code), rests[i].quantity, rests[i].itemLevel, static_cast<uint32_t>(made));
		}
	}
}

// UI thread: the SDK names the local player only here
void PickPlayer(const D2RL::PluginContext*, void*) noexcept {
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	const bool named = g_inventory->getLocalPlayer(g_context, &player) == D2RL::Inventory::Result::Success;
	if (named) {
		g_localPlayer = player;
	}
	if (!named || g_threads->runOnGameThread(g_context, MakeRests, reinterpret_cast<void*>(player)) != D2RL::Threads::Result::Success) {
		plog::Warn("sockets: could not queue the rest of a stack; it is lost");
		std::lock_guard<std::mutex> lock(g_restLock);
		g_restCount = 0;
	}
}

void QueueRest(Unit* filler, int32_t left) noexcept {
	using namespace d2r::rva;
	bool first = false;
	{
		std::lock_guard<std::mutex> lock(g_restLock);
		if (g_restCount >= MaxRests) {
			plog::Warn("sockets: too many rests queued; %d items lost", left);
			return;
		}
		first = g_restCount == 0;
		g_rests[g_restCount++] = {d2r::Engine<uint32_t (*)(Unit*) noexcept>(ItemCode)(filler), static_cast<uint32_t>(left),
		                          d2r::Engine<uint32_t (*)(Unit*) noexcept>(ItemLevel)(filler)};
	}
	if (!first) {
		return;
	}
	const D2RL::PlayerHandle kept = g_localPlayer;
	const bool queued = kept != D2RL::InvalidPlayerHandle
	                        ? g_threads->runOnGameThread(g_context, MakeRests, reinterpret_cast<void*>(kept)) == D2RL::Threads::Result::Success
	                        : g_threads->runOnUiThread(g_context, PickPlayer, nullptr) == D2RL::Threads::Result::Success;
	if (!queued) {
		plog::Warn("sockets: could not queue the rest of a stack; it is lost");
		std::lock_guard<std::mutex> lock(g_restLock);
		g_restCount = 0;
	}
}

auto Quantity(Unit* item) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(item, d2r::rva::StatQuantity, 0);
}

void SetQuantity(Unit* item, int32_t value) noexcept {
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::rva::SetUnitStat)(item, d2r::rva::StatQuantity, value, 0);
}

// Native rest (seamless, any player): the packet handler (0x4f9f40) is wrapped, and once it returns the rest is made with
// ItemSpawn and put on the cursor with D2R's own placer, so the client gets "into the socket" and "on the cursor" in one
// tick. Not inside the insert: for a runeword the handler then takes the filler back out to the cursor (0x389820, 0x471500)
// and replays the insert later (0x4b1a29), so the filler id must stay the unit that goes in, and the cursor stays free.
constexpr const char* HandlerPattern = "40 55 53 57 41 54 48 8d 6c 24 d1 48 81 ec f8 00 00 00";
constexpr uint8_t HandlerEntry[] = {0x40, 0x55, 0x53, 0x57, 0x41, 0x54, 0x48, 0x8d, 0x6c, 0x24, 0xd1};
// (player, locations, item ids, count, flag) -> placed: the handler's put-back to the cursor
constexpr const char* PlacePattern = "48 89 5c 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8d ac 24 f0 68 ff ff";
// The imbue's free item call (0x43ec10 (game, item), `E8 rel32` at +10; loot_rules.cpp resolves it the same way)
constexpr const char* FreeCallPattern =
	"48 8b 54 24 60 48 8b 4c 24 58 e8 ?? ?? ?? ?? "
	"48 8b 4c 24 58 48 8d 95 20 02 00 00 45 33 c0 e8 ?? ?? ?? ?? 48 89 44 24 60 48 85 c0 75 ?? 48 8d 4c 24 ?? 88 44 24 "
	"?? e8 ?? ?? ?? ?? 84 c0 74 01 cc 48 8b 44 24 60 41 b8 08 19 00 00 48 8d 15 ?? ?? ?? ?? 48 8b c8 e8 ?? ?? ?? ?? "
	"48 8b 54 24 60 45 33 c0 48 8b 4c 24 58 89 44 24 7c e8 ?? ?? ?? ?? 48 8b 54 24 60 45 33 c0 48 8b 4c 24 58 e8 ?? ?? ?? ?? "
	"48 8b 4c 24 60 33 d2 e8";
// after its ItemSpawn the imbue readies the new unit for the game and its clients with three calls before placing it
// (0x46ea70 (game, item, 0), 0x53bb50 (game, item, 0), 0x43c910 (item, 0)); without them the placer moves a unit no client knows
constexpr uint32_t FreeCallAt = 10, ReadyACallAt = 0x6b, ReadyBCallAt = 0x7d, ReadyCCallAt = 0x89;
// ItemSpawn's drop request (gheed_store.cpp): +0 unit, +0x10 game, +0x18 ilvl, +0x20 items row, +0x24 mode, +0x38 1,
// +0x3a the game's u16 +0x128, +0x40 quality; mode 4 with no room gives a free unit (page 0xff), as the insert needs
constexpr int ReqGame = 0x10, ReqItemLevel = 0x18, ReqRow = 0x20, ReqMode = 0x24, ReqOne = 0x38, ReqGameWord = 0x3a,
              ReqQuality = 0x40, ReqSize = 0xc8, GameWord = 0x128;
constexpr int32_t QualityNormal = 2;
constexpr uint32_t ItemFlagIdentified = 0x10;
constexpr int32_t CursorLocation[4] = {4, 0xff, 0, 0};  // what 0x382e30 builds for the handler

using HandlerFn = uint64_t (*)(void* game, Unit* player, uint8_t* packet, uint8_t direct, int64_t* a5, int64_t* a6) noexcept;
using PlaceFn = uint8_t (*)(Unit* player, const int32_t* locations, const uint32_t* ids, int32_t count, uint8_t flag) noexcept;
using FreeFn = void (*)(void* game, Unit* item) noexcept;
using ReadyFn = void (*)(void* game, Unit* item, int32_t zero) noexcept;
using ReadyItemFn = void (*)(Unit* item, int32_t zero) noexcept;
ReadyFn g_readyA = nullptr, g_readyB = nullptr;
ReadyItemFn g_readyC = nullptr;
HandlerFn g_handler = nullptr;
PlaceFn g_place = nullptr;
FreeFn g_free = nullptr;
std::atomic<uint32_t> g_native {0};

// set by the insert inside the handler, read once it returns (game thread only)
struct Pending {
	uint32_t fillerId;
	int32_t quantity;
};
thread_local bool g_inHandler = false;
thread_local Pending g_pending {0, 0};

void Fallback(Unit* player, Unit* filler, int32_t left) noexcept {
	using namespace d2r::rva;
	if (At<uint32_t>(player, UnitIdField) == LocalPlayerId()) {
		QueueRest(filler, left);
	} else {
		plog::Warn("sockets: the rest of a remote player's stack, %d items, not made again", left);
	}
}

// The rest straight onto the cursor; false when it could not be made or placed (nothing is left over)
auto PlaceRest(void* game, Unit* player, Unit* filler, int32_t left) noexcept -> bool {
	using namespace d2r::rva;
	alignas(8) uint8_t req[ReqSize] {};
	*reinterpret_cast<Unit**>(req) = player;
	*reinterpret_cast<void**>(req + ReqGame) = game;
	const auto level = static_cast<int32_t>(d2r::Engine<uint32_t (*)(Unit*) noexcept>(ItemLevel)(filler));
	*reinterpret_cast<int32_t*>(req + ReqItemLevel) = level > 0 ? level : 1;
	*reinterpret_cast<int32_t*>(req + ReqRow) = At<int32_t>(filler, UnitClassId);
	*reinterpret_cast<int32_t*>(req + ReqMode) = ItemModeCursor;
	*reinterpret_cast<uint16_t*>(req + ReqOne) = 1;
	*reinterpret_cast<uint16_t*>(req + ReqGameWord) = At<uint16_t>(game, GameWord);
	*reinterpret_cast<int32_t*>(req + ReqQuality) = QualityNormal;
	Unit* rest = d2r::Engine<Unit* (*)(void*, void*, int32_t) noexcept>(d2r::sig::ItemSpawn)(game, req, 0);
	if (rest == nullptr) {
		return false;
	}
	g_readyA(game, rest, 0);
	g_readyB(game, rest, 0);
	g_readyC(rest, 0);
	d2r::Engine<void (*)(Unit*, uint32_t, int32_t) noexcept>(d2r::sig::SetItemFlag)(rest, ItemFlagIdentified, 1);
	SetQuantity(rest, left);
	const uint32_t id = At<uint32_t>(rest, UnitIdField);
	if (g_place(player, CursorLocation, &id, 1, 0) == 0) {
		g_free(game, rest);
		return false;
	}
	return true;
}

auto Handler(void* game, Unit* player, uint8_t* packet, uint8_t direct, int64_t* a5, int64_t* a6) noexcept -> uint64_t {
	using namespace d2r::rva;
	g_pending = {0, 0};
	g_inHandler = true;
	const uint64_t result = g_handler(game, player, packet, direct, a5, a6);
	g_inHandler = false;
	const Pending pending = g_pending;
	g_pending = {0, 0};
	if (pending.quantity <= 1) {
		return result;
	}
	Unit* filler = d2r::Engine<Unit* (*)(void*, int32_t, uint32_t) noexcept>(FindUnit)(game, 4, pending.fillerId);
	if (filler == nullptr) {
		return result;
	}
	if (At<int32_t>(filler, UnitMode) == ItemModeCursor) {
		// a runeword: the handler took it back to the cursor and replays the insert, which splits it again.
		// shortcut: the client shows the stack as 1 until that replay; resend it (0x535f60) if the replay can be skipped
		SetQuantity(filler, pending.quantity);
		return result;
	}
	g_split.fetch_add(1);
	if (PlaceRest(game, player, filler, pending.quantity - 1)) {
		g_native.fetch_add(1);
	} else {
		Fallback(player, filler, pending.quantity - 1);
	}
	return result;
}

auto Insert(void* game, Unit* player, uint32_t fillerId, uint32_t parentId, uint8_t flag) noexcept -> int32_t {
	using namespace d2r::rva;
	Unit* filler = d2r::Engine<Unit* (*)(void*, int32_t, uint32_t) noexcept>(FindUnit)(game, 4, fillerId);
	// only a filler held on the cursor (mode 4): the save loaders (PlrSave.cpp) call the insert for socketed children too
	const int32_t quantity = IsItem(filler) && At<int32_t>(filler, UnitMode) == ItemModeCursor ? Quantity(filler) : 0;
	if (quantity <= 1 || player == nullptr) {
		return g_insert(game, player, fillerId, parentId, flag);
	}
	const bool native = g_inHandler && g_handler != nullptr;
	if (!native && At<uint32_t>(player, UnitIdField) != LocalPlayerId()) {
		g_refusedRemote.fetch_add(1);  // no rest can be made for a remote player, so its stack stays on the cursor
		return 0;
	}
	SetQuantity(filler, 1);
	const int32_t placed = g_insert(game, player, fillerId, parentId, flag);
	// in a socket: the insert set its mode to 6 (a cursor item already has page 0xff, so the page tells nothing)
	if (At<int32_t>(filler, UnitMode) != ItemModeSocketed) {
		SetQuantity(filler, quantity);  // refused: the stack stays whole
	} else if (native) {
		g_pending = {fillerId, quantity};
	} else {
		g_split.fetch_add(1);
		QueueRest(filler, quantity - 1);
	}
	return placed;
}

// The handler hook and its two engine calls; false leaves the SDK rest (next game update, local player only)
auto InstallNativeRest(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t place = 0, free = 0;
	if (d2r::sig::ItemSpawn.state != sigscan::State::Ok || d2r::sig::SetItemFlag.state != sigscan::State::Ok ||
	    sigscan::ScanUnique(context->exeBase, PlacePattern, 0, &place) != 1 ||
	    sigscan::ScanUnique(context->exeBase, FreeCallPattern, 0, &free) != 1) {
		return false;
	}
	const auto target = [&](uint32_t at) noexcept -> uint64_t {
		const auto* call = reinterpret_cast<const uint8_t*>(context->exeBase + free + at);
		int32_t rel = 0;
		std::memcpy(&rel, call + 1, sizeof(rel));
		return call[0] == 0xe8 ? reinterpret_cast<uint64_t>(call + 5 + rel) : 0;
	};
	const uint64_t freeFn = target(FreeCallAt), a = target(ReadyACallAt), b = target(ReadyBCallAt), c = target(ReadyCCallAt);
	if (freeFn == 0 || a == 0 || b == 0 || c == 0) {
		return false;
	}
	g_free = reinterpret_cast<FreeFn>(freeFn);
	g_readyA = reinterpret_cast<ReadyFn>(a);
	g_readyB = reinterpret_cast<ReadyFn>(b);
	g_readyC = reinterpret_cast<ReadyItemFn>(c);
	g_place = reinterpret_cast<PlaceFn>(context->exeBase + place);
	return sigscan::HookBySignature(context, "SocketPacket", HandlerPattern, 0, HandlerEntry, sizeof(HandlerEntry), &Handler, &g_handler);
}

auto InstallStackInsert(const D2RL::PluginContext* context) noexcept -> bool {
	if (context->QueryService(&g_items) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasItemServiceField(g_items, D2RL::ItemServiceRequiredSize) ||
	    context->QueryService(&g_inventory) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasInventoryServiceField(g_inventory, D2RL::InventoryServiceRequiredSize) ||
	    context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize)) {
		return false;
	}
	g_context = context;
	// without it every rest takes the UI-thread path (PickPlayer)
	const D2RL::LifecycleService* lifecycle = nullptr;
	const D2RL::Lifecycle::GameplayEventListener ready {.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
	                                                    .kind = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
	                                                    .callback = KeepLocalPlayer};
	D2RL::Lifecycle::ListenerHandle readyHandle = D2RL::Lifecycle::InvalidHandle;
	if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) ||
	    lifecycle->registerGameplayEventListener(context, &ready, &readyHandle) != D2RL::Lifecycle::Result::Success) {
		plog::Warn("sockets: no LocalPlayerReady listener; the rest of a stack takes the UI-thread path");
	}
	if (!sigscan::HookBySignature(context, "SocketInsert", InsertPattern, 0, InsertEntry, sizeof(InsertEntry), &Insert, &g_insert)) {
		return false;
	}
	plog::Info("sockets: the rest of a socketed stack is made %s", InstallNativeRest(context) ? "natively, on the cursor in the same tick"
	                                                                                          : "through the SDK (native rest off)");
	return true;
}

}  // namespace

auto MarkSocketedCompact(Unit* filler) noexcept -> bool {
	using namespace d2r::rva;
	if (!IsItem(filler)) {
		return false;
	}
	const auto isType = d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(ItemIsType);
	if (isType(filler, ItemTypeGem) == 0 && isType(filler, ItemTypeRune) == 0) {
		return false;
	}
	auto* data = d2r::Engine<uint8_t* (*)(Unit*) noexcept>(UnitItemData)(filler);
	if (data == nullptr) {
		return false;
	}
	auto& flags = *reinterpret_cast<uint32_t*>(data + ItemDataFlags);
	if ((flags & ItemFlagCompactSave) != 0) {
		return false;
	}
	flags |= ItemFlagCompactSave;
	plog::Sample(g_marked);
	return true;
}

auto InstallSocketRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	const bool hook = context->InstallInlineHook(SocketFillerMods, SocketFillerModsEntry, sizeof(SocketFillerModsEntry),
	                                             SocketMods, &g_original);
	const bool stackable = InstallStackableSockets(context);
	const bool insert = InstallStackInsert(context);
	std::snprintf(report, reportSize, "socket mods once%s; stackable sockets%s; stacks socket one%s",
	              hook ? "" : " FAILED (bytes differ)", stackable ? "" : " FAILED (signature, service or relay)",
	              insert ? "" : " FAILED (signature or service)");
	return (hook ? 1 : 0) + (stackable ? 1 : 0) + (insert ? 1 : 0);
}

auto SocketRuleCount() noexcept -> int {
	return 3;
}

auto SocketRuleCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "socketed fillers marked compact %u, stackables let roll sockets %u, stacks socketed one %u (native rest %u, remote refused %u)",
	              g_marked.load(), g_stackRolls.load(), g_split.load(), g_native.load(), g_refusedRemote.load());
}
