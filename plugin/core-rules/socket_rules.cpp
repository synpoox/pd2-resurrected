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
#include <D2RLPlugin/network.h>
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

// The rest of a socketed stack, made again on the game thread through the SDK. The local player's handle comes from the UI
// thread (as stash_stacks.cpp); a remote player's from its peer (getPeerPlayer): each client's core-rules tells the host its
// player unit id over a private channel once its player is ready. A remote stacked insert with no known peer is refused, so
// nothing is lost. Leaving the game before the queued job runs (one update) loses the rest.
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
void Reconnect(const D2RL::PluginContext* context) noexcept;
// kept from LocalPlayerReady, so the rest is made on the next game update with no UI-thread hop first
std::atomic<D2RL::PlayerHandle> g_localPlayer {D2RL::InvalidPlayerHandle};

void KeepLocalPlayer(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	g_localPlayer = g_inventory->getLocalPlayer(context, &player) == D2RL::Inventory::Result::Success ? player
	                                                                                                   : D2RL::InvalidPlayerHandle;
	Reconnect(context);
}

// server and client unit ids match, in a local or a hosted game
auto LocalPlayerId() noexcept -> uint32_t {
	using namespace d2r::rva;
	Unit* local = d2r::Engine<Unit* (*)(uint32_t) noexcept>(ClientPlayer)(d2r::Engine<uint32_t (*)() noexcept>(LocalPlayerSlot)());
	return local != nullptr ? At<uint32_t>(local, UnitIdField) : 0xffffffffu;
}

void PickPlayer(const D2RL::PluginContext*, void*) noexcept;

// Game thread: one rest to the player's cursor, else the inventory, else the ground
auto CreateRest(D2RL::PlayerHandle player, const Rest& rest) noexcept -> D2RL::Items::Result {
	namespace items = D2RL::Items;
	items::Result made = items::Result::Success;
	for (const auto container : {items::ItemContainer::Cursor, items::ItemContainer::Inventory, items::ItemContainer::Ground}) {
		const items::ItemCreateSpec spec {.structSize = items::ItemCreateSpecSize,
		                                  .code = rest.code,
		                                  .quality = items::Quality::Normal,
		                                  .qualityRecordId = items::RandomQualityRecord,  // 0 is refused (InvalidArgument)
		                                  .itemLevel = rest.itemLevel > 0 ? rest.itemLevel : 1,
		                                  .seedMode = items::SeedMode::Random,
		                                  // a single keeps the native default (the stat may be absent on one)
		                                  .quantity = rest.quantity > 1 ? rest.quantity : items::DefaultValue,
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
	return made;
}

// Remote players (hosted TCP/IP game): unit id -> the peer that named it
namespace net = D2RL::Network;
constexpr uint16_t ChannelId = 1;
constexpr uint64_t ChannelToken = 0x5044325232313139ull;  // "PD2R2119"; client and host must match
constexpr uint16_t MessagePlayerId = 1;
const D2RL::NetworkService* g_network = nullptr;
net::ChannelHandle g_channel = net::InvalidChannelHandle;
struct PeerEntry {
	uint32_t unitId;
	net::PeerHandle peer;
};
constexpr int MaxPeers = 16;
PeerEntry g_peers[MaxPeers] {};
std::mutex g_peerLock;
struct RemoteRest {
	Rest rest;
	uint32_t owner;
};
RemoteRest g_remote[MaxRests];
int g_remoteCount = 0;
std::atomic<int> g_connectTries {0};

auto PeerOf(uint32_t unitId) noexcept -> net::PeerHandle {
	std::lock_guard<std::mutex> lock(g_peerLock);
	for (const PeerEntry& e : g_peers) {
		if (e.peer != net::InvalidPeerHandle && e.unitId == unitId) {
			return e.peer;
		}
	}
	return net::InvalidPeerHandle;
}

// Host: a client names its player unit id (a later name for the same peer replaces the old one)
void OnHostMessage(const D2RL::PluginContext*, net::ChannelHandle, net::PeerHandle peer, uint16_t id, const void* data,
                   uint32_t size, void*) noexcept {
	if (id != MessagePlayerId || size != sizeof(uint32_t) || peer == net::InvalidPeerHandle) {
		return;
	}
	uint32_t unitId = 0;
	std::memcpy(&unitId, data, sizeof(unitId));
	std::lock_guard<std::mutex> lock(g_peerLock);
	for (const PeerEntry& e : g_peers) {
		if (e.peer != net::InvalidPeerHandle && e.peer != peer && e.unitId == unitId) {
			return;  // first claim wins: another client cannot take a player's rests (the SDK cannot check a claim)
		}
	}
	PeerEntry* slot = nullptr;
	for (PeerEntry& e : g_peers) {
		if (e.peer == peer) {
			e.peer = net::InvalidPeerHandle;  // one entry per peer
		}
		if (slot == nullptr && e.peer == net::InvalidPeerHandle) {
			slot = &e;
		}
	}
	if (slot != nullptr) {
		*slot = {unitId, peer};
	}
}

void OnClientMessage(const D2RL::PluginContext*, net::ChannelHandle, uint16_t, const void*, uint32_t, void*) noexcept {}

void Connect(const D2RL::PluginContext* context, void*) noexcept;
std::atomic<bool> g_connected {false};

void OnConnection(const D2RL::PluginContext* context, const net::ConnectionEvent* event, void*) noexcept {
	const bool clientSide = event->peer == net::InvalidPeerHandle;
	if (clientSide && event->state == net::ConnectionState::Connected) {
		g_connected = true;
	} else if (clientSide && !g_connected && (event->state == net::ConnectionState::Disconnected ||
	                                          (event->state == net::ConnectionState::Rejected &&
	                                           event->reason == net::RejectionReason::HostUnavailable))) {
		// a time-out or HostUnavailable before connecting: connect again (README), within the same try budget
		if (g_connectTries.fetch_add(1) < 600) {
			g_threads->runOnUiThread(context, Connect, nullptr);
		}
		return;
	} else if (clientSide && event->state == net::ConnectionState::Disconnected) {
		g_connected = false;
	}
	if (event->state == net::ConnectionState::Connected) {
		// client side (and the host's own in-process connection): name this player to the host
		const uint32_t unitId = LocalPlayerId();
		if (unitId != 0xffffffffu) {
			g_network->sendToHost(context, g_channel, MessagePlayerId, &unitId, sizeof(unitId));
		}
	} else if (event->state == net::ConnectionState::Disconnected && event->peer != net::InvalidPeerHandle) {
		std::lock_guard<std::mutex> lock(g_peerLock);
		for (PeerEntry& e : g_peers) {
			if (e.peer == event->peer) {
				e.peer = net::InvalidPeerHandle;
			}
		}
	}
}

// UI thread: connect to the host once the player is ready; a TCP client may get Busy until the host's transport packet
// arrives, so it retries on the next UI frames (about 10 seconds)
void Connect(const D2RL::PluginContext* context, void*) noexcept {
	if (g_network == nullptr || g_channel == net::InvalidChannelHandle) {
		return;
	}
	const net::Result result = g_network->connectToHost(context, g_channel);
	if ((result == net::Result::Busy || result == net::Result::Unavailable) && g_connectTries.fetch_add(1) < 600) {
		g_threads->runOnUiThread(context, Connect, nullptr);
	} else if (result != net::Result::Success && result != net::Result::Conflict) {
		plog::Warn("sockets: no host channel (result %u); a remote player's stacked sockets are refused", static_cast<uint32_t>(result));
	}
}

void Reconnect(const D2RL::PluginContext* context) noexcept {
	{
		// a new game: the host makes it before any client joins, so no peer of an earlier game is kept
		std::lock_guard<std::mutex> lock(g_peerLock);
		for (PeerEntry& e : g_peers) {
			e.peer = net::InvalidPeerHandle;
		}
	}
	g_connected = false;
	g_connectTries = 0;
	Connect(context, nullptr);
}

// Game thread: each remote rest for its player's handle, named by the peer
void MakeRemoteRests(const D2RL::PluginContext* context, void*) noexcept {
	RemoteRest rests[MaxRests];
	int count = 0;
	{
		std::lock_guard<std::mutex> lock(g_restLock);
		std::memcpy(rests, g_remote, sizeof(RemoteRest) * g_remoteCount);
		count = g_remoteCount;
		g_remoteCount = 0;
	}
	for (int i = 0; i < count; ++i) {
		D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
		const net::PeerHandle peer = PeerOf(rests[i].owner);
		const bool named = peer != net::InvalidPeerHandle &&
		                   g_network->getPeerPlayer(context, g_channel, peer, &player) == net::Result::Success;
		const D2RL::Items::Result made = named ? CreateRest(player, rests[i].rest) : D2RL::Items::Result::NotFound;
		if (made != D2RL::Items::Result::Success) {
			plog::Warn("sockets: the rest of player %u's stack, %.4s x%u, not made again: result %u", rests[i].owner,
			           reinterpret_cast<const char*>(&rests[i].rest.code), rests[i].rest.quantity, static_cast<uint32_t>(made));
		}
	}
}

void QueueRemoteRest(Unit* filler, int32_t left, uint32_t owner) noexcept {
	using namespace d2r::rva;
	bool first = false;
	{
		std::lock_guard<std::mutex> lock(g_restLock);
		if (g_remoteCount >= MaxRests) {
			plog::Warn("sockets: too many rests queued; %d items lost", left);
			return;
		}
		first = g_remoteCount == 0;
		g_remote[g_remoteCount++] = {{d2r::Engine<uint32_t (*)(Unit*) noexcept>(ItemCode)(filler), static_cast<uint32_t>(left),
		                              d2r::Engine<uint32_t (*)(Unit*) noexcept>(ItemLevel)(filler)},
		                             owner};
	}
	if (first && g_threads->runOnGameThread(g_context, MakeRemoteRests, nullptr) != D2RL::Threads::Result::Success) {
		plog::Warn("sockets: could not queue the rest of a remote stack; it is lost");
		std::lock_guard<std::mutex> lock(g_restLock);
		g_remoteCount = 0;
	}
}

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
		const items::Result made = CreateRest(player, rests[i]);
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

auto Insert(void* game, Unit* player, uint32_t fillerId, uint32_t parentId, uint8_t flag) noexcept -> int32_t {
	using namespace d2r::rva;
	Unit* filler = d2r::Engine<Unit* (*)(void*, int32_t, uint32_t) noexcept>(FindUnit)(game, 4, fillerId);
	// only a filler held on the cursor (mode 4): the save loaders (PlrSave.cpp) call the insert for socketed children too
	const int32_t quantity = IsItem(filler) && At<int32_t>(filler, UnitMode) == ItemModeCursor ? Quantity(filler) : 0;
	if (quantity <= 1 || player == nullptr) {
		return g_insert(game, player, fillerId, parentId, flag);
	}
	const uint32_t owner = At<uint32_t>(player, UnitIdField);
	const bool local = owner == LocalPlayerId();
	bool room = false;
	{
		std::lock_guard<std::mutex> lock(g_restLock);
		room = (local ? g_restCount : g_remoteCount) < MaxRests;
	}
	if (!room || (!local && PeerOf(owner) == net::InvalidPeerHandle)) {
		// no rest could be made (no handle for that player, or the queue is full), so refused: D2R's put-back keeps the stack whole
		g_refusedRemote.fetch_add(1);
		return 0;
	}
	SetQuantity(filler, 1);
	const int32_t placed = g_insert(game, player, fillerId, parentId, flag);
	// in a socket: the insert set its mode to 6 (a cursor item already has page 0xff, so the page tells nothing)
	if (At<int32_t>(filler, UnitMode) == ItemModeSocketed) {
		g_split.fetch_add(1);
		if (local) {
			QueueRest(filler, quantity - 1);
		} else {
			QueueRemoteRest(filler, quantity - 1, owner);
		}
	} else {
		SetQuantity(filler, quantity);  // refused: the stack stays whole
	}
	return placed;
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
	const net::ChannelRegistration channel {.structSize = net::ChannelRegistrationSize,
	                                        .localChannelId = ChannelId,
	                                        .compatibilityToken = ChannelToken,
	                                        .hostMessage = OnHostMessage,
	                                        .clientMessage = OnClientMessage,
	                                        .connectionState = OnConnection};
	if (context->QueryService(&g_network) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasNetworkServiceField(g_network, D2RL::NetworkServiceRequiredSize) ||
	    g_network->registerChannel(context, &channel, &g_channel) != net::Result::Success) {
		g_network = nullptr;
		plog::Warn("sockets: no network channel; a remote player's stacked sockets are refused");
	}
	return sigscan::HookBySignature(context, "SocketInsert", InsertPattern, 0, InsertEntry, sizeof(InsertEntry), &Insert, &g_insert);
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
	std::snprintf(out, size, "socketed fillers marked compact %u, stackables let roll sockets %u, stacks socketed one %u (remote refused %u)",
	              g_marked.load(), g_stackRolls.load(), g_split.load(), g_refusedRemote.load());
}
