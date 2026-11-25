// Shared by the stash affinity files in this folder: the state and types, and the functions one file takes from
// another. Moved out of stash_affinity.cpp (#2198); the code is unchanged.
#pragma once
// A quick-moved currency or boss part tops its cell's stack up to its cap, as a hand drop does (#1758, #1771); the rest
// comes back to the inventory. A map's stack still takes only a whole stack that fits (MapRefusal).
#include "../stash_affinity.h"

#include "log.h"
#include "log_once.h"
#include "map_stack.h"
#include "pd2_engine.h"
#include "stash_home.h"
#include "../stash_tabs.h"

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/network.h>
#include <D2RLPlugin/threads.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace stash_affinity {

namespace detail {

namespace items = D2RL::Items;
namespace aff = pd2::affinity;
using C = items::ItemContainer;

inline const D2RL::PluginContext* g_context = nullptr;
inline const D2RL::ItemService* g_items = nullptr;
inline const D2RL::InventoryService* g_inventory = nullptr;
inline const D2RL::ThreadService* g_threads = nullptr;

constexpr uint32_t Stashes = items::ContainerBit(C::PersonalStash) | items::ContainerBit(C::SharedStash);
constexpr D2RL::Inventory::ItemFilter InventoryOnly {.structSize = D2RL::Inventory::ItemFilterSize,
                                                     .containerMask = items::ContainerBit(C::Inventory)};
constexpr D2RL::Inventory::ItemFilter StashOnly {.structSize = D2RL::Inventory::ItemFilterSize, .containerMask = Stashes};
constexpr uint32_t ReservedPages = aff::LastReservedPage + 1 - aff::FirstReservedPage;
constexpr uint32_t TidyEvery = 25;  // updates between tidies while a reserved tab is open

// The routed items in the inventory at the last update (game thread only). An inventory holds at most 10 x 8 items.
struct Noted {
	uint32_t runtimeId;
	uint32_t code;
	D2RL::ItemHandle handle;  // good for the update that noted it
};
constexpr uint32_t MaxNoted = 128;
struct NotedList {
	uint32_t count = 0;
	Noted item[MaxNoted];
};
inline NotedList g_noted;
inline NotedList g_kept;  // big stacks Withdraw could not send home: not tried again while they stay in the inventory (#1763)

// One stash pass (game thread only): the noted items that left the inventory, as found in a stash, and the reserved
// pages' items with the cells they use (items there are 1 x 1, but for the Bossing page's)
constexpr uint32_t MaxReserved = ReservedPages * aff::PageWidth * aff::PageWidth;
struct Pass {
	NotedList gone;
	uint32_t foundCount = 0;
	items::ItemInfo found[MaxNoted];
	uint32_t reservedCount = 0;
	items::ItemInfo reserved[MaxReserved];
	uint32_t used[ReservedPages][aff::PageWidth] {};  // reserved page -> row -> a bit per used column
	int32_t at[2][aff::PageWidth][aff::PageWidth] {};  // Maps, Bossing page: row, column -> index in reserved; -1 empty, -2 used
};
inline Pass g_pass;

// Remote players (host only; #2186): each client's peer, whether its Maps or Bossing tab is open, and the inventory
// stacks last seen. handle and open are set by network callbacks; the lists are the game thread's.
namespace net = D2RL::Network;
constexpr uint16_t ChannelId = 1, MessageTabOpen = 1;
constexpr uint64_t ChannelToken = 0x5044325232313836ull;  // "PD2R2186"; client and host must match
constexpr int MaxPeers = 16;
struct Peer {
	std::atomic<net::PeerHandle> handle {net::InvalidPeerHandle};
	std::atomic<bool> open {false};
	net::PeerHandle seen = net::InvalidPeerHandle;  // the handle the lists belong to (game thread only)
	NotedList noted, kept;
};
inline Peer g_peers[MaxPeers];
inline std::mutex g_peerLock;  // slot claims and releases
inline const D2RL::NetworkService* g_network = nullptr;
inline net::ChannelHandle g_channel = net::InvalidChannelHandle;
inline std::atomic<bool> g_connected {false};
inline std::atomic<bool> g_tabDirty {false}, g_sendLogged {false};  // the last tab state did not reach the host
inline std::atomic<int> g_connectTries {0};

inline std::atomic<uintptr_t> g_generation {0};
inline uintptr_t g_seen = 0;  // the watch generation g_noted belongs to (game thread only)
inline uint32_t g_shown = 0;  // the base whose maps are on the Maps page (game thread only)
inline std::atomic<uint32_t> g_want {0};
inline std::atomic<uint32_t> g_glow {0};  // TakeGlow's bits: the reserved tabs quick-moved items went home to (#2116)
inline std::atomic<uint32_t> g_mapGlow[aff::Tiers] = {};  // TakeMapGlow's bits: per tier, the base of each quick-moved map gone home
static_assert(aff::PageWidth <= 32);
inline std::atomic<bool> g_open {false};
inline std::atomic<uint32_t> g_viewPage {0};
inline std::atomic<bool> g_tidyFailed {false};
inline pd2::LogOnce g_failLogged;  // game thread only: a retry loop writes a line once (#2094)
inline std::atomic<bool> g_hinted {false}, g_stopped {false};
inline std::atomic<uint32_t> g_handStacked {0};
inline std::atomic<uint32_t> g_routed {0}, g_merged {0}, g_swapped {0}, g_returned {0}, g_failed {0}, g_updates {0};
inline uint32_t g_handTaken = 0;  // game thread: the stack TakeOut moved last (a hand take, whose piece belongs on the cursor)

struct Find {
	uint32_t id;
	items::ItemInfo info {};
};

auto InCell(int32_t x, int32_t y) noexcept -> bool;
auto Reserved(const items::ItemInfo& item) noexcept -> bool;
auto At(uint32_t page, int32_t row, int32_t column) noexcept -> int32_t&;
void Use(uint32_t page, int32_t x, int32_t y, bool used) noexcept;
auto NoteInventory(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction;
auto ReadStash(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction;
auto Move(D2RL::PlayerHandle player, D2RL::ItemHandle item, const items::ItemDestination& to) noexcept -> items::Result;
auto MoveTo(D2RL::PlayerHandle player, items::ItemInfo& item, uint32_t page, int32_t x, int32_t y) noexcept -> items::Result;
auto FreeMapCell() noexcept -> aff::Cell;
auto ToStorage(D2RL::PlayerHandle player, items::ItemInfo& item) noexcept -> items::Result;
auto Quantity(D2RL::ItemHandle item) noexcept -> int32_t;
auto SetQuantity(D2RL::PlayerHandle player, D2RL::ItemHandle item, int32_t count) noexcept -> items::Result;
auto MapRefusal(const items::ItemInfo& a, const items::ItemInfo& b, uint32_t stackPage, bool inView, bool shown) noexcept -> const char*;
auto Merge(D2RL::PlayerHandle player, items::ItemInfo& item, items::ItemInfo& stack, bool topUp = false) noexcept -> items::Result;
auto FindStored(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction;
void ResendCursor(D2RL::PlayerHandle player, uint32_t cursorId) noexcept;
void StackNow(const D2RL::PluginContext*, void* user) noexcept;
void SyncReserved(const items::ItemInfo& item) noexcept;
auto Route(D2RL::PlayerHandle player, items::ItemInfo item) noexcept -> bool;
auto QuickWithdraw(D2RL::PlayerHandle player, const items::ItemInfo& item, int32_t had) noexcept -> bool;
void Withdraw(D2RL::PlayerHandle player, const Noted& noted) noexcept;
auto Listed(const NotedList& list, uint32_t runtimeId) noexcept -> bool;
void Swap(D2RL::PlayerHandle player, uint32_t want) noexcept;
void Tidy(D2RL::PlayerHandle player) noexcept;
void OnHostMessage(const D2RL::PluginContext*, net::ChannelHandle, net::PeerHandle peer, uint16_t id, const void* data,
                   uint32_t size, void*) noexcept;
void OnClientMessage(const D2RL::PluginContext*, net::ChannelHandle, uint16_t, const void*, uint32_t, void*) noexcept;
void SendTabOpen(bool open) noexcept;
void OnConnection(const D2RL::PluginContext* context, const net::ConnectionEvent* event, void*) noexcept;
void __cdecl StartWatch(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept;

}  // namespace detail

using namespace detail;

}  // namespace stash_affinity
