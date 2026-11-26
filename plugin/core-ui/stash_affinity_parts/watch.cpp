// The watch's update, remote peers and the host messages.
// Moved verbatim from stash_affinity.cpp (#2198).
#include "shared.h"

namespace stash_affinity {

namespace detail {

void ResetPass() noexcept {
	g_pass.foundCount = 0;
	g_pass.reservedCount = 0;
	for (auto& page : g_pass.used) {
		for (uint32_t& row : page) {
			row = 0;
		}
	}
	for (auto& page : g_pass.at) {
		for (auto& row : page) {
			for (int32_t& cell : row) {
				cell = -1;
			}
		}
	}
}

// Host, game thread (#2186): a remote player with its tab open has its new stacks past WithdrawMax cut by QuickWithdraw
// (the cursor split of Withdraw is a hand take, which a remote player does not report). The host's Maps view is not
// theirs: g_shown is 0 for the pass, so a map goes to its storage row.
void UpdatePeers() noexcept {
	if (g_network == nullptr) {
		return;
	}
	D2RL::PlayerHandle local = D2RL::InvalidPlayerHandle;
	g_inventory->getLocalPlayer(g_context, &local);
	for (Peer& peer : g_peers) {
		const net::PeerHandle handle = peer.handle.load();
		if (handle != peer.seen) {  // a new peer, or a slot reused: runtime ids of the last one mean nothing here
			peer.seen = handle;
			peer.noted.count = peer.kept.count = 0;
		}
		if (!peer.open.load()) {  // closed: nothing is cut; the stacks already held count as new when it opens, as for the host
			peer.noted.count = peer.kept.count = 0;
			continue;
		}
		D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
		if (handle == net::InvalidPeerHandle ||
		    g_network->getPeerPlayer(g_context, g_channel, handle, &player) != net::Result::Success || player == local) {
			continue;  // the host's own in-process connection is the local player, handled by Update
		}
		NotedList now;
		if (g_inventory->forEachInventoryItem(g_context, player, &InventoryOnly, NoteInventory, &now) != D2RL::Inventory::Result::Success) {
			continue;
		}
		NotedList big, kept, noted;
		for (uint32_t j = 0; j < now.count; ++j) {
			const Noted& n = now.item[j];
			if (Listed(peer.kept, n.runtimeId)) {
				kept.item[kept.count++] = n;
			} else if (!Listed(peer.noted, n.runtimeId) && aff::StackCap(n.code) > aff::WithdrawMax && aff::Stacks(n.code) &&
			           Quantity(n.handle) > aff::WithdrawMax) {
				big.item[big.count++] = n;
			}
			noted.item[noted.count++] = n;
		}
		peer.kept = kept;
		peer.noted = noted;
		if (big.count == 0) {
			continue;
		}
		ResetPass();
		if (g_inventory->forEachInventoryItem(g_context, player, &StashOnly, ReadStash, &g_pass) != D2RL::Inventory::Result::Success) {
			continue;
		}
		const uint32_t shown = g_shown;
		g_shown = 0;
		for (uint32_t i = 0; i < big.count; ++i) {
			items::ItemInfo item {.structSize = items::ItemInfoSize};
			if (g_items->getItemInfo(g_context, big.item[i].handle, &item) == items::Result::Success &&
			    !QuickWithdraw(player, item, Quantity(item.handle)) && peer.kept.count < MaxNoted) {
				peer.kept.item[peer.kept.count++] = big.item[i];  // not tried again while it stays
			}
		}
		g_shown = shown;
	}
}

void Update(const D2RL::PluginContext*, void* user) noexcept {
	const auto generation = reinterpret_cast<uintptr_t>(user);
	if (generation != g_generation.load()) {
		return;  // a newer game started its own watch
	}
	if (const auto queued = g_threads->runOnGameThread(g_context, Update, user); queued != D2RL::Threads::Result::Success) {
		if (!g_stopped.exchange(true)) {
			plog::Info("affinity: the watch stopped (%u) until the next game", static_cast<uint32_t>(queued));
		}
		return;
	}
	if (generation != g_seen) {  // a new game: runtime ids from the last one mean nothing here
		g_seen = generation;
		g_noted.count = 0;
		g_kept.count = 0;
		g_shown = 0;
	}
	const uint32_t update = ++g_updates;
	UpdatePeers();
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success) {
		g_noted.count = 0;
		return;
	}
	NotedList now;
	if (g_inventory->forEachInventoryItem(g_context, player, &InventoryOnly, NoteInventory, &now) != D2RL::Inventory::Result::Success) {
		return;
	}
	g_pass.gone.count = 0;
	for (uint32_t i = 0; i < g_noted.count; ++i) {
		bool kept = false;
		for (uint32_t j = 0; j < now.count && !kept; ++j) {
			kept = now.item[j].runtimeId == g_noted.item[i].runtimeId;
		}
		if (!kept) {
			g_pass.gone.item[g_pass.gone.count++] = g_noted.item[i];
		}
	}
	// stacks past WithdrawMax new in the inventory (#1763); kept ones that left it may be tried again later
	// with the stash closed they wait unnoted: a 50 on the cursor out of a panel was dropped by the next click (Slot5)
	NotedList big;
	NotedList kept;
	NotedList noted;
	const bool open = g_open.load();
	for (uint32_t j = 0; j < now.count; ++j) {
		const Noted& n = now.item[j];
		if (Listed(g_kept, n.runtimeId)) {
			kept.item[kept.count++] = n;
		} else if (!Listed(g_noted, n.runtimeId) && aff::StackCap(n.code) > aff::WithdrawMax && aff::Stacks(n.code) &&
		           Quantity(n.handle) > aff::WithdrawMax) {
			if (!open) {
				continue;
			}
			big.item[big.count++] = n;
		}
		noted.item[noted.count++] = n;
	}
	g_kept = kept;
	g_noted = noted;
	const uint32_t want = g_want.load();
	const bool swap = want != g_shown, tidy = g_open.load() && update % TidyEvery == 0;
	if (g_pass.gone.count == 0 && !swap && !tidy && big.count == 0) {
		return;
	}
	ResetPass();
	if (g_inventory->forEachInventoryItem(g_context, player, &StashOnly, ReadStash, &g_pass) != D2RL::Inventory::Result::Success) {
		return;
	}
	if (swap) {
		Swap(player, want);
	}
	for (uint32_t i = 0; i < g_pass.foundCount; ++i) {
		if (Route(player, g_pass.found[i])) {
			g_glow.fetch_or(aff::HomeOf(g_pass.found[i].code).category == aff::Category::Bossing ? GlowBossing : GlowMaps);
			if (const aff::Base b = aff::BaseOf(g_pass.found[i].code); b.tier >= 0) {
				g_mapGlow[b.tier].fetch_or(1u << b.index);
			}
		}
	}
	for (uint32_t i = 0; i < big.count; ++i) {
		Withdraw(player, big.item[i]);
	}
	if (tidy) {
		Tidy(player);
	}
}

// Host: a client names its tab state (1 byte). The first message of a peer claims a slot.
void OnHostMessage(const D2RL::PluginContext*, net::ChannelHandle, net::PeerHandle peer, uint16_t id, const void* data,
                   uint32_t size, void*) noexcept {
	if (id != MessageTabOpen || size != 1 || peer == net::InvalidPeerHandle) {
		return;
	}
	const bool open = *static_cast<const uint8_t*>(data) != 0;
	std::lock_guard<std::mutex> lock(g_peerLock);
	Peer* slot = nullptr;
	for (Peer& p : g_peers) {
		if (p.handle.load() == peer) {
			slot = &p;
			break;
		}
		if (slot == nullptr && p.handle.load() == net::InvalidPeerHandle) {
			slot = &p;
		}
	}
	if (slot != nullptr) {
		slot->open = open;
		slot->handle = peer;
	}
}

void OnClientMessage(const D2RL::PluginContext*, net::ChannelHandle, uint16_t, const void*, uint32_t, void*) noexcept {}

void SendTabOpen(bool open) noexcept {
	if (g_network != nullptr && g_connected.load()) {
		const uint8_t value = open ? 1 : 0;
		// a host or single player is not connected, so has no one to tell; a failed send is retried by the next SetTabOpen
		const net::Result sent = g_network->sendToHost(g_context, g_channel, MessageTabOpen, &value, 1);
		g_tabDirty = sent != net::Result::Success && sent != net::Result::WrongRole;  // a host has no host to tell
		if (g_tabDirty && !g_sendLogged.exchange(true)) {
			plog::Info("affinity: the tab state was not sent to the host (result %u); retrying", static_cast<uint32_t>(sent));
		}
	}
}

void Connect(const D2RL::PluginContext* context, void*) noexcept {
	if (g_network == nullptr || g_channel == net::InvalidChannelHandle) {
		return;
	}
	const net::Result result = g_network->connectToHost(context, g_channel);
	if ((result == net::Result::Busy || result == net::Result::Unavailable) && g_connectTries.fetch_add(1) < 600) {
		g_threads->runOnUiThread(context, Connect, nullptr);  // a TCP client gets Busy until the host's transport packet arrives
	}
}

// The loader gives the client's own events a valid peer too (the host, 2), so the two sides are told apart by what they do:
// a remote peer's disconnect frees its slot (only the host has slots), and any Connected event sends the tab state, which
// only a client can (a host gets WrongRole, not an error).
void OnConnection(const D2RL::PluginContext* context, const net::ConnectionEvent* event, void*) noexcept {
	if (event->state == net::ConnectionState::Connected) {
		g_connected = true;
		SendTabOpen(g_open.load());
		return;
	}
	if (event->state == net::ConnectionState::Disconnected && event->peer != net::InvalidPeerHandle) {
		std::lock_guard<std::mutex> lock(g_peerLock);
		for (Peer& p : g_peers) {
			if (p.handle.load() == event->peer) {
				p.open = false;
				p.handle = net::InvalidPeerHandle;
			}
		}
	}
	if (!g_connected && (event->state == net::ConnectionState::Disconnected ||
	                     (event->state == net::ConnectionState::Rejected && event->reason == net::RejectionReason::HostUnavailable))) {
		if (g_connectTries.fetch_add(1) < 600) {
			g_threads->runOnUiThread(context, Connect, nullptr);
		}
	} else if (event->state == net::ConnectionState::Disconnected) {
		g_connected = false;
	}
}

void __cdecl StartWatch(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	const uintptr_t generation = ++g_generation;
	g_stopped = false;
	{
		std::lock_guard<std::mutex> lock(g_peerLock);  // a new game: the host makes it before any client joins
		for (Peer& p : g_peers) {
			p.open = false;
			p.handle = net::InvalidPeerHandle;
		}
	}
	g_connected = false;
	g_connectTries = 0;
	Connect(context, nullptr);
	if (const auto queued = g_threads->runOnGameThread(context, Update, reinterpret_cast<void*>(generation));
	    queued != D2RL::Threads::Result::Success) {
		plog::Info("affinity: no game thread (%u): routing runs in single player and on the host only", static_cast<uint32_t>(queued));
	}
}

}  // namespace detail

}  // namespace stash_affinity
