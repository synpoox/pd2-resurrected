// Stash affinity (#1553), all through the PluginSDK (no D2R hook). D2R's own quick-move (Ctrl+click, or the pad's)
// puts an inventory item into the stash tab on show; then a game-thread watch moves a map, bossing or map-material item
// on to its reserved shared stash page and cell (common/stash_home.h): a map to its base's row on its tier's storage
// page, or onto the Maps page when the Maps tab shows its base; a map currency to its cell beside the cube, merged into
// the stack there; a bossing item to the Bossing page. The watch notes those items in the inventory each game update;
// one that is gone from the inventory and in a stash on the next update was quick-moved there (a hand move passes
// through the cursor, so it is never routed). A full or missing page moves the item back to the inventory.
// executeExistingItemTransaction moves the item itself, so its rolls, sockets and quantity stay. A homed stack onto its
// cell's stack merges up to MaxStack (#1744); the counts are read off the native items (Quantity below).
//
// The Maps tab (stash_tabs.cpp) names the base it shows (ShowBase); the next update moves the other bases' maps off
// the Maps page to their storage rows and the shown base's maps from its storage page onto the Maps page's map cells.
//
// Why not ItemInteractionService: it raised no event for an inventory click on this loader (almanac.cpp), and it does
// not see the pad's quick-move. Why not a hook: the SDK's moves, edits and inventory reads cover it; only the stack
// count is read natively, through the SDK's editNativeItem.
//
// A TCP/IP client has no game thread, so routing and the base swap run in single player and on the host only. The one
// remote part is the quick-move withdraw (#2186): a client tells the host when its Maps or Bossing tab opens or closes
// (a private channel), and with it open the host watches that peer's own inventory (getPeerPlayer) for a new stack past
// WithdrawMax and cuts it as for its own player (QuickWithdraw); counts are read on the server, never sent by the client.
// shortcut: a remote player's deposits are not routed home and its Maps tab shows no base (its maps go to their storage
// row); upgrade by sending the shown base to the host.
// shortcut: no chat line when a page is full (the item just comes back to the inventory, and the log says why); add
// one with a string if players miss it.
#include "stash_affinity_parts/shared.h"

namespace stash_affinity {

namespace {

template <class Service>
auto Query(const Service*& service, bool (*has)(const Service*, uint32_t), uint32_t required) noexcept -> bool {
	if (g_context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !has(service, required)) {
		service = nullptr;
	}
	return service != nullptr;
}

}  // namespace

void ShowBase(uint32_t code) noexcept {
	g_want.store(code);
}

void SetTabOpen(bool open, uint32_t page) noexcept {
	if (g_open.exchange(open) != open || g_tabDirty.load()) {
		SendTabOpen(open);
	}
	g_viewPage.store(open ? page : 0);
}

auto ViewPage() noexcept -> uint32_t {
	return g_viewPage.load();
}

auto StackOnto(uint32_t cursorId, uint32_t targetId) noexcept -> bool {
	if (g_threads == nullptr || g_inventory == nullptr || g_items == nullptr) {
		return false;
	}
	void* ids = reinterpret_cast<void*>(static_cast<uintptr_t>(cursorId) << 32 | targetId);
	return g_threads->runOnGameThread(g_context, StackNow, ids) == D2RL::Threads::Result::Success;
}

auto Resend(uint32_t itemId) noexcept -> bool {
	if (g_threads == nullptr || g_inventory == nullptr || g_items == nullptr) {
		return false;
	}
	const auto now = [](const D2RL::PluginContext*, void* user) noexcept {
		D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
		if (g_inventory->getLocalPlayer(g_context, &player) == D2RL::Inventory::Result::Success) {
			ResendCursor(player, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(user)));
		}
	};
	return g_threads->runOnGameThread(g_context, now, reinterpret_cast<void*>(static_cast<uintptr_t>(itemId))) ==
	       D2RL::Threads::Result::Success;
}

auto TakeOut(uint32_t itemId) noexcept -> bool {
	if (g_threads == nullptr || g_inventory == nullptr || g_items == nullptr) {
		return false;
	}
	const auto now = [](const D2RL::PluginContext*, void* user) noexcept {
		D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
		Find stack {.id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(user))};
		if (g_inventory->getLocalPlayer(g_context, &player) != D2RL::Inventory::Result::Success ||
		    (g_inventory->forEachInventoryItem(g_context, player, &StashOnly, FindStored, &stack), stack.info.runtimeId != stack.id)) {
			plog::Info("affinity: take of item %u: it is gone from the stash; nothing moved", stack.id);
			return;
		}
		// shortcut: a full inventory leaves the stack in its cell (logged); upgrade with a chat line if players miss it
		const items::ItemDestination bag {.structSize = items::ItemDestinationSize, .container = C::Inventory,
		                                  .placement = items::Placement::Automatic};
		const items::ItemInfo& on = stack.info;
		const auto moved = Move(player, on.handle, bag);
		if (moved == items::Result::Success) {
			g_handTaken = on.runtimeId;
		}
		if (moved != items::Result::Success) {
			// the consumed take left the stack pending on the client (0x1c6ac0) until an item packet: a move onto its own
			// cell sends one, else anywhere on its page (the tidy pass routes it home)
			items::ItemDestination home {.structSize = items::ItemDestinationSize, .container = C::SharedStash,
			                             .placement = items::Placement::Exact, .sharedStashPage = on.sharedStashPage};
			home.x = static_cast<uint32_t>(on.x);
			home.y = static_cast<uint32_t>(on.y);
			auto resent = Move(player, on.handle, home);
			if (resent != items::Result::Success) {
				home.placement = items::Placement::Automatic;
				resent = Move(player, on.handle, home);
			}
			if (resent != items::Result::Success) {
				plog::Warn("affinity: %08x stays in the stash; sent to the client again by a move on page %u: result %u",
				           on.code, on.sharedStashPage, static_cast<uint32_t>(resent));
			}
		}
	};
	return g_threads->runOnGameThread(g_context, now, reinterpret_cast<void*>(static_cast<uintptr_t>(itemId))) ==
	       D2RL::Threads::Result::Success;
}

auto TakeGlow() noexcept -> uint32_t {
	return g_glow.exchange(0);
}

auto TakeMapGlow(int32_t tier) noexcept -> uint32_t {
	return tier >= 0 && tier < aff::Tiers ? g_mapGlow[tier].exchange(0) : 0;
}

auto WantedBase() noexcept -> uint32_t {
	return g_want.load();
}

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	g_context = context;
	const D2RL::LifecycleService* lifecycle = nullptr;
	// `&`, not `&&`: every service is queried, so the report never blames a missing one by mistake
	if (!(Query(g_items, D2RL::HasItemServiceField, D2RL::ItemServiceSplitStackFieldEnd) &  // Withdraw splits (#1763)
	      Query(g_inventory, D2RL::HasInventoryServiceField, D2RL::InventoryServiceRequiredSize) &
	      Query(g_threads, D2RL::HasThreadServiceField, D2RL::ThreadServiceRequiredSize) &
	      Query(lifecycle, D2RL::HasLifecycleServiceField, D2RL::LifecycleServiceRequiredSize))) {
		std::snprintf(report, size, "FAILED (an item, inventory, thread or lifecycle service is missing)");
		return;
	}
	sigscan::Entry* const sigs[] = {&d2r::sig::SetUnitStat};  // SetQuantity past 50 (#1763); unresolved: stacks stop at 50
	sigscan::Resolve(sigs, 1, context->exeBase, nullptr, 0);
	if (!D2RL::HasItemServiceCapability(g_items, items::ItemServiceCapability::SharedStashWrite)) {
		std::snprintf(report, size, "FAILED (the item service cannot write the shared stash)");
		return;
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
		plog::Info("affinity: no network channel; a remote player's quick-move is not cut to 50");
	}
	const D2RL::Lifecycle::GameplayEventListener listener {.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
	                                                       .kind = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
	                                                       .callback = StartWatch};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	if (lifecycle->registerGameplayEventListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
		std::snprintf(report, size, "FAILED (no player-ready listener)");
		return;
	}
	std::snprintf(report, size, "quick-moved maps, bossing and map materials go to shared tabs %u-%u (add_shared_tabs >= %u)",
	              aff::FirstReservedPage, aff::LastReservedPage, aff::RequiredExtraTabs);
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size,
	              "affinity: %u routed (%u merged, %u hand stacks), %u base swaps, %u sent back to the inventory, %u stuck in the stash; %u updates",
	              g_routed.load(), g_merged.load(), g_handStacked.load(), g_swapped.load(), g_returned.load(), g_failed.load(), g_updates.load());
}

}  // namespace stash_affinity
