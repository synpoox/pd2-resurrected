// The Sacrifice Altar's panel (#1192). PD2's altar takes a corrupted amulet through a panel; this is the D2R version,
// built only on SDK services: a 1x1 custom player page (InventoryService) bound to the grid of a left panel
// (PanelService, layout files/data/global/ui/layouts/core-maps/SacrificeAltarhd.json), the buttons caught as UI messages
// (SharedEventService), and the item work as item transactions (ItemService). No hook, no address.
//
// Pad focus (#1456): a world open skips D2R's controller steps, so after the open the frame callback repeats what the
// stash panel does when it opens in pad mode (BankPanel open handler, 93847 0x23cc40): reset the held input
// (0x8d540), open the ControllerOverlay if it is not up, else refresh it, focus the panel (0x858050), once the
// inventory panel beside it is up too. docs/research/controller/altar-world-focus.md.
//
// Flow: the altar's operate (lucion.cpp) opens the panel. Confirm moves the amulet from the page to the inventory, runs
// Desecrate on it there (lucion.cpp; a failed roll makes the brick) and closes every panel. The X and Escape just
// close the panel: a frame watcher returns any amulet left on the page, which also covers a save-and-exit with the
// slot filled.
// shortcut: host only (the SDK names the local player on the UI thread only), as the brick was; a remote use is refused in the log.
#include "altar_panel.h"

#include "pd2_engine.h"
#include "log.h"
#include "lucion.h"
#include "sig.h"

#include <D2RLPlugin/inventory.h>
#include <D2RLPlugin/item.h>
#include <D2RLPlugin/overlay.h>
#include <D2RLPlugin/panels.h>
#include <D2RLPlugin/shared_events.h>
#include <D2RLPlugin/threads.h>
#include <D2RLPlugin/widgets.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>

namespace {

namespace items = D2RL::Items;
namespace inv = D2RL::Inventory;
using d2r::At;

// Never change PageId: saved characters key the page's items by it.
constexpr char PageId[] = "sacrifice-altar";
constexpr char PanelId[] = "SacrificeAltar";
constexpr char SlotName[] = "core-maps/AltarSlot";
constexpr char ConfirmText[] = "core-maps/AltarConfirm";
constexpr uint32_t WatchFrames = 30;

constexpr char FullPanelName[] = "core-maps/SacrificeAltar";
constexpr uint32_t FocusWaitFrames = 120;

// D2R's own pieces of the stash's pad open, found by signature (93847 RVAs for reference only). Any miss leaves the
// panel without pad focus, as before.
// 93847: 0x858050, void (panel): the focus manager takes the open panel
sigscan::Entry FocusPanel {"PanelFocus", "altar", sigscan::Kind::Code, 0, 0,
                           "40 53 48 83 ec 20 48 8b d9 e8 ?? ?? ?? ?? 48 8b d3 48 8b c8 48 83 c4 20 5b e9 ?? ?? ?? ?? cc cc 48 89 5c 24 10"};
// 93847: 0x846170, void* (name): PanelManager::FetchPanel
sigscan::Entry FetchPanel {"PanelFetch", "altar", sigscan::Kind::Code, 0, 0, "48 8b d1 48 8b 0d ?? ?? ?? ?? 48 85 c9 0f 85 ?? ?? ?? ??"};
// 93847: 0x8d540, void (): clears the held movement and button state
sigscan::Entry ResetInput {"InputReset", "altar", sigscan::Kind::Code, 0, 0,
                           "8b 0d ?? ?? ?? ?? 33 c0 89 05 ?? ?? ?? ?? 89 05 ?? ?? ?? ?? 88 05 ?? ?? ?? ?? 89 05 ?? ?? ?? ?? 89 05 ?? ?? ?? ?? 88 05 ?? ?? ?? ?? c7 05 ?? ?? ?? ?? 10 00 00 00 c7 05 ?? ?? ?? ?? 10 00 00 00 e9 ?? ?? ?? ??"};
// 93847: 0x77e10, bool (): the pad is the input in use
sigscan::Entry PadMode {"PadMode", "altar", sigscan::Kind::Code, 0, 0, "48 83 ec 28 e8 ?? ?? ?? ?? 80 b8 c8 0d 00 00 00 75 10"};
// 93847: 0x857c00, void (panel): clears the panel's focus (the UI switcher does it to the old panel)
sigscan::Entry ClearFocus {"PanelFocusClear", "altar", sigscan::Kind::Code, 0, 0,
                           "40 53 48 83 ec 20 48 8b d9 e8 ?? ?? ?? ?? 48 8b d3 48 8b c8 48 83 c4 20 5b e9 ?? ?? ?? ?? cc cc 48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 49 8b f8"};
// 93847: 0x846020, void* (): the focus manager; its +0x198 is the focused panel
sigscan::Entry FocusManager {"FocusManager", "altar", sigscan::Kind::Code, 0, 0,
                             "48 83 ec 28 48 8b 05 ?? ?? ?? ?? 48 85 c0 75 1a 48 8d 4c 24 30 88 44 24 30 e8 ?? ?? ?? ?? 84 c0 74 01 cc 48 8b 05 ?? ?? ?? ?? 48 8b 80 d0 00 00 00"};
// 93847: 0x1f6e50 (overlay, int), 0x1f5d60 (overlay), 0x1f5a80 (overlay, legend): the ControllerOverlay's refresh calls
sigscan::Entry OverlayA {"OverlayA", "altar", sigscan::Kind::Code, 0, 0, "48 89 74 24 10 57 48 83 ec 20 8b f2 48 8d 15 ?? ?? ?? ??"};
sigscan::Entry OverlayB {"OverlayB", "altar", sigscan::Kind::Code, 0, 0, "40 56 57 41 57 48 83 ec 20 48 8b f1"};
sigscan::Entry OverlayC {"OverlayC", "altar", sigscan::Kind::Code, 0, 0,
                         "40 55 57 41 56 48 8b ec 48 81 ec 80 00 00 00 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 45 f8"};
sigscan::Entry* const FocusSigs[] = {&FocusPanel, &FetchPanel, &ResetInput, &PadMode, &ClearFocus, &FocusManager, &OverlayA, &OverlayB, &OverlayC};
bool g_focusSigs = false;
bool g_focusPending = false;  // UI thread only
uint32_t g_focusWait = 0;
uint32_t g_focusReady = 0;  // frames both panels have been active
void* g_focusedPanel = nullptr;
uint32_t g_focusCheck = 0;  // frames left before the focus manager is read back

const D2RL::PluginContext* g_context = nullptr;
const D2RL::PanelService* g_panels = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ItemService* g_items = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;
D2RL::Panels::RegistrationHandle g_panel = D2RL::Panels::InvalidHandle;
const uint32_t AmuletCode = items::MakeItemCode('a', 'm', 'u');

d2r::Game* g_game = nullptr;  // the game and player that used the altar, for the Desecration
d2r::Unit* g_unit = nullptr;
std::atomic<bool> g_returning {false};
std::atomic<bool> g_returnFailLogged {false};
std::atomic<uint32_t> g_acceptedId {0};  // the local player's unit id, set by PickPlayer
uint32_t g_frames = 0;
std::atomic<uint32_t> g_opened {0}, g_confirmed {0}, g_returned {0};

auto OnItemPolicy(const D2RL::PluginContext*, const inv::ItemPolicyRequest* request, void*) noexcept -> inv::ItemPolicyDecision {
	if (inv::ItemPolicyRequestMatchesType(request, inv::MakeItemTypeFourCC("amul"))) {
		return inv::ItemPolicyDecision::Allow;
	}
	return request->reason == inv::ItemPlacementReason::SavedItemRestore ? inv::ItemPolicyDecision::RemoveOnly
	                                                                      : inv::ItemPolicyDecision::Reject;
}

// The amulet on a custom page. charm-inv's page holds charms only, so the amulet code finds ours.
auto FindAmulet(const D2RL::PluginContext*, const items::ItemInfo* item, void* user) noexcept -> inv::IterationAction {
	if (item->code == AmuletCode) {
		*static_cast<D2RL::ItemHandle*>(user) = item->handle;
		return inv::IterationAction::Stop;
	}
	return inv::IterationAction::Continue;
}

auto SlotAmulet(D2RL::PlayerHandle player) noexcept -> D2RL::ItemHandle {
	D2RL::ItemHandle amulet = D2RL::InvalidItemHandle;
	const inv::ItemFilter filter {.structSize = inv::ItemFilterSize,
	                              .containerMask = items::ContainerBit(items::ItemContainer::CustomPage)};
	g_inventory->forEachInventoryItem(g_context, player, &filter, FindAmulet, &amulet);
	return amulet;
}

// Game thread: the amulet back into the inventory, same handle and identity
auto MoveToInventory(D2RL::PlayerHandle player, D2RL::ItemHandle item) noexcept -> bool {
	items::ExistingItemOperation op {};
	op.structSize = items::ExistingItemOperationSize;
	op.kind = items::ExistingItemOperationKind::Move;
	op.item = item;
	op.move.destination = {.structSize = items::ItemDestinationSize,
	                       .container = items::ItemContainer::Inventory,
	                       .placement = items::Placement::Automatic};
	const items::ExistingItemTransaction transaction {
		.structSize = items::ExistingItemTransactionSize, .player = player, .operationCount = 1, .operations = &op};
	items::ExistingItemTransactionResult result {.structSize = items::ExistingItemTransactionResultSize};
	const items::Result status = g_items->executeExistingItemTransaction(g_context, &transaction, &result);
	if (status != items::Result::Success) {
		plog::Info("altar: the amulet did not move to the inventory (%u)", static_cast<uint32_t>(status));
	}
	return status == items::Result::Success;
}

// UI thread: every panel closed (the altar's, and an inventory that was open before it)
void CloseAll(const D2RL::PluginContext*, void*) noexcept {
	g_panels->closePanel(g_context, g_panel);
	const D2RL::Widgets::UiAction close {.structSize = D2RL::Widgets::UiActionSize,
	                                     .target = "PlayerInventoryPanelMessage",
	                                     .command = "Close",
	                                     .text = ""};
	const auto status = g_widgets->dispatchUiAction(g_context, &close);
	plog::Info("altar: panels closed (inventory close message %u)", static_cast<uint32_t>(status));
}

struct Roll {
	DesecrateResult result = DesecrateResult::Refused;
};

void RollNative(const D2RL::PluginContext*, void* native, void* user) noexcept {
	static_cast<Roll*>(user)->result = Desecrate(g_game, g_unit, static_cast<d2r::Unit*>(native));
}

// Game thread: confirm
void ConfirmAltar(const D2RL::PluginContext*, void* user) noexcept {
	const auto player = reinterpret_cast<D2RL::PlayerHandle>(user);
	++g_confirmed;
	const D2RL::ItemHandle amulet = SlotAmulet(player);
	if (amulet == D2RL::InvalidItemHandle) {
		plog::Info("altar: confirm with an empty slot");
	} else if (g_game == nullptr || g_unit == nullptr || At<uint32_t>(g_unit, d2r::rva::UnitIdField) != g_acceptedId.load()) {
		plog::Info("altar: confirm refused, the stored player is not the local one; the amulet stays for the watcher");
	} else if (MoveToInventory(player, amulet)) {
		Roll roll;
		const items::Result edited = g_items->editNativeItem(g_context, amulet, RollNative, &roll);
		if (edited != items::Result::Success) {
			plog::Info("altar: the amulet could not be read (%u)", static_cast<uint32_t>(edited));
		} else if (roll.result == DesecrateResult::Failed) {
			BrickAmulet(g_game, g_unit, player, amulet);
		}
	}
	g_threads->runOnUiThread(g_context, CloseAll, nullptr);
}

// Game thread: X, Escape, a replaced panel or a rejoin left the amulet on the page
void ReturnAltarItem(const D2RL::PluginContext*, void* user) noexcept {
	const auto player = reinterpret_cast<D2RL::PlayerHandle>(user);
	const D2RL::ItemHandle amulet = SlotAmulet(player);
	if (amulet != D2RL::InvalidItemHandle) {
		if (MoveToInventory(player, amulet)) {
			plog::Info("altar: amulet %llu returned to the inventory", static_cast<unsigned long long>(amulet));
			++g_returned;
		} else {
			g_returnFailLogged.store(true);  // one log per streak: no retry until the panel opens again
		}
	}
	g_returning.store(false, std::memory_order_release);
}

auto OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
	-> D2RL::SharedEvents::UiMessageAction {
	using Action = D2RL::SharedEvents::UiMessageAction;
	if (event == nullptr || event->target == nullptr || event->command == nullptr || event->text == nullptr ||
	    std::strcmp(event->target, "PanelManager") != 0 || std::strcmp(event->command, "OpenPanel") != 0 ||
	    std::strcmp(event->text, ConfirmText) != 0) {
		return Action::Continue;
	}
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (g_inventory->getLocalPlayer(g_context, &player) != inv::Result::Success ||
	    g_threads->runOnGameThread(g_context, ConfirmAltar, reinterpret_cast<void*>(player)) != D2RL::Threads::Result::Success) {
		plog::Info("altar: confirm could not be queued");
	}
	return Action::Consume;
}

// UI thread, each frame while a pad open waits: the stash's pad steps once the altar and the inventory beside it are active
void FocusAltarPanel() noexcept {
	auto fetch = reinterpret_cast<void* (*)(const char*) noexcept>(FetchPanel.address);
	auto active = [](void* p) { return p != nullptr && At<uint8_t>(p, 0x51) != 0; };
	void* panel = fetch(FullPanelName);
	void* inventory = fetch("PlayerInventoryExpansionLayout");
	if (!active(inventory)) {
		inventory = fetch("PlayerInventoryOriginalLayout");
	}
	if (!active(panel) || !active(inventory)) {
		g_focusReady = 0;
		if (++g_focusWait > FocusWaitFrames) {
			g_focusPending = false;
			plog::Info("altar: pad focus gave up, the panel or the inventory never became active");
		}
		return;
	}
	if (++g_focusReady < 2) {  // one more frame, so the inventory's own open has finished
		return;
	}
	g_focusPending = false;
	if (!reinterpret_cast<bool (*)() noexcept>(PadMode.address)()) {
		return;
	}
	reinterpret_cast<void (*)() noexcept>(ResetInput.address)();
	void* overlay = fetch("ControllerOverlay");  // the stash only refreshes it, and only if it is up
	if (!active(overlay)) {
		// The stash's decompile builds an OpenPanel message for "ControllerOverlay" in pad mode (0x23cc40); a slot check with
		// neither the open nor an attached overlay showed no pad frame and no help bar.
		const D2RL::Widgets::UiAction open {.structSize = D2RL::Widgets::UiActionSize,
		                                    .target = "PanelManager",
		                                    .command = "OpenPanel",
		                                    .text = "ControllerOverlay"};
		g_widgets->dispatchUiAction(g_context, &open);
	} else {
		reinterpret_cast<void (*)(void*, int) noexcept>(OverlayA.address)(overlay, At<int>(overlay, 0x438));
		reinterpret_cast<void (*)(void*) noexcept>(OverlayB.address)(overlay);
		reinterpret_cast<void (*)(void*, int) noexcept>(OverlayC.address)(overlay, 0x18);
		reinterpret_cast<void (*)(void*, int) noexcept>(OverlayC.address)(overlay, 1);
	}
	reinterpret_cast<void (*)(void*) noexcept>(ClearFocus.address)(inventory);
	reinterpret_cast<void (*)(void*) noexcept>(FocusPanel.address)(panel);
	g_focusedPanel = panel;
	g_focusCheck = 5;
	plog::Info("altar: pad focus set %u frames after the open (overlay %s)", g_focusWait, active(overlay) ? "refreshed" : "not up");
}

// UI thread: a few frames after the focus, who holds it
void CheckFocus() noexcept {
	if (--g_focusCheck != 0) {
		return;
	}
	void* manager = reinterpret_cast<void* (*)() noexcept>(FocusManager.address)();
	void* held = manager != nullptr ? At<void*>(manager, 0x198) : nullptr;
	plog::Info("altar: focus manager holds %p, the altar panel is %p (%s)", held, g_focusedPanel,
	           held == g_focusedPanel ? "same" : "different");
}

// UI thread, every WatchFrames frames: a closed panel with an amulet still on the page returns it
void OnFrame(const D2RL::PluginContext*, const D2RL::Overlay::Frame*, void*) noexcept {
	if (g_focusPending) {
		FocusAltarPanel();
	}
	if (g_focusCheck != 0) {
		CheckFocus();
	}
	if (++g_frames % WatchFrames != 0 || g_returning.load(std::memory_order_acquire) || g_returnFailLogged.load()) {
		return;
	}
	D2RL::Panels::PanelInfo info {.structSize = D2RL::Panels::PanelInfoSize};
	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	if (g_panels->getPanelInfo(g_context, g_panel, &info) != D2RL::Panels::Result::Success ||
	    info.presentationState != D2RL::Panels::PresentationState::Closed ||
	    g_inventory->getLocalPlayer(g_context, &player) != inv::Result::Success || SlotAmulet(player) == D2RL::InvalidItemHandle) {
		return;
	}
	g_returning.store(true, std::memory_order_release);
	if (g_threads->runOnGameThread(g_context, ReturnAltarItem, reinterpret_cast<void*>(player)) != D2RL::Threads::Result::Success) {
		g_returning.store(false, std::memory_order_release);
	}
}

struct Use {
	d2r::Game* game;
	d2r::Unit* unit;
	uint32_t id;
};

// UI thread: the SDK names the local player only here; opens the panel for the player who used the altar
void PickPlayer(const D2RL::PluginContext*, void* user) noexcept {
	using namespace d2r::rva;
	const std::unique_ptr<Use> use {static_cast<Use*>(user)};
	const uint32_t usedBy = use->id;
	g_returning.store(false, std::memory_order_release);  // a lost ReturnAltarItem must not stop the watcher for good
	d2r::Unit* local = d2r::Engine<d2r::Unit* (*)(uint32_t) noexcept>(ClientPlayer)(d2r::Engine<uint32_t (*)() noexcept>(LocalPlayerSlot)());
	if (local == nullptr || At<uint32_t>(local, UnitIdField) != usedBy) {
		plog::Info("altar: used by another player than this game's own, no panel");
		return;
	}
	g_game = use->game;  // only the local player's pair is kept: a remote click must not swap it
	g_unit = use->unit;
	g_acceptedId.store(usedBy);
	const auto status = g_panels->openPanel(g_context, g_panel);
	plog::Info("altar: panel %s (%u)", status == D2RL::Panels::Result::Success ? "opened" : "NOT opened",
	           static_cast<uint32_t>(status));
	g_returnFailLogged.store(false);
	if (status == D2RL::Panels::Result::Success) {
		++g_opened;
		const bool pad = g_focusSigs && reinterpret_cast<bool (*)() noexcept>(PadMode.address)();
		plog::Info("altar: pad mode %d, focus signatures %s", pad ? 1 : 0, g_focusSigs ? "ok" : "off");
		if (pad) {
			g_focusPending = true;
			g_focusWait = 0;
			g_focusReady = 0;
		}
	}
}

template <class Service>
auto Query(const D2RL::PluginContext* context, const Service*& service, bool (*has)(const Service*, uint32_t),
           uint32_t required) noexcept -> bool {
	return context->QueryService(&service) == D2RL::ServiceQueryResult::Success && has(service, required);
}

}  // namespace

auto OpenAltarPanel(d2r::Game* game, d2r::Unit* player) noexcept -> void {
	if (g_threads == nullptr || g_panel == D2RL::Panels::InvalidHandle) {
		plog::Info("altar: used, no panel (the SDK panel services are off)");
		return;
	}
	auto* use = new (std::nothrow) Use {game, player, At<uint32_t>(player, d2r::rva::UnitIdField)};
	if (use == nullptr) {
		return;
	}
	if (g_threads->runOnUiThread(g_context, PickPlayer, use) != D2RL::Threads::Result::Success) {
		delete use;
		plog::Info("altar: the panel could not be queued");
	}
}

auto InstallAltarPanel(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	const D2RL::SharedEventService* events = nullptr;
	const D2RL::OverlayService* overlay = nullptr;
	if (!(Query(context, g_panels, D2RL::HasPanelServiceField, D2RL::PanelServiceRequiredSize) &&
	      Query(context, g_inventory, D2RL::HasInventoryServiceField, D2RL::InventoryServiceRequiredSize) &&
	      Query(context, g_items, D2RL::HasItemServiceField, D2RL::ItemServiceRequiredSize) &&
	      Query(context, g_threads, D2RL::HasThreadServiceField, D2RL::ThreadServiceRequiredSize) &&
	      Query(context, g_widgets, D2RL::HasWidgetServiceField, D2RL::WidgetServiceRequiredSize) &&
	      Query(context, events, D2RL::HasSharedEventServiceField, D2RL::SharedEventServiceRequiredSize) &&
	      Query(context, overlay, D2RL::HasOverlayServiceField, D2RL::OverlayServiceRequiredSize))) {
		g_threads = nullptr;
		std::snprintf(report, reportSize, "off: an SDK panel, inventory, item, widget, event or overlay service is missing");
		return 0;
	}
	g_context = context;
	sigscan::Resolve(FocusSigs, std::size(FocusSigs), context->exeBase, nullptr, 0);
	g_focusSigs = true;
	for (const sigscan::Entry* e : FocusSigs) {
		if (e->state != sigscan::State::Ok) {
			g_focusSigs = false;
			plog::Info("altar: signature %s %s, no pad focus", e->name, sigscan::StateText(e->state));
		}
	}
	inv::PageRegistrationHandle page = inv::InvalidHandle;
	const inv::PlayerPageRegistration registration {.structSize = inv::PlayerPageRegistrationSize, .flags = 0, .logicalId = PageId};
	const inv::PlayerPageConfiguration size {.structSize = inv::PlayerPageConfigurationSize, .flags = 0, .width = 1, .height = 1};
	const D2RL::Panels::PanelRegistration panel {.structSize = D2RL::Panels::PanelRegistrationSize,
	                                             .flags = D2RL::Panels::PanelFlags::GameplayLeftSlot | D2RL::Panels::PanelFlags::CloseOnEscape,
	                                             .localId = PanelId};
	const D2RL::SharedEvents::UiMessageListener listener {.structSize = D2RL::SharedEvents::UiMessageListenerSize,
	                                                      .callback = OnUiMessage};
	const D2RL::Overlay::CallbackRegistration frame {.structSize = D2RL::Overlay::CallbackRegistrationSize,
	                                                 .phase = D2RL::Overlay::Phase::AfterGameUi,
	                                                 .callback = OnFrame};
	D2RL::SharedEvents::ListenerHandle listenerHandle = D2RL::SharedEvents::InvalidHandle;
	D2RL::Overlay::CallbackHandle frameHandle = D2RL::Overlay::InvalidCallbackHandle;
	const auto fail = [&](const char* step, int code) {
		g_threads = nullptr;
		std::snprintf(report, reportSize, "off: %s was refused (result %d)", step, code);
		return 0;
	};
	if (const auto r = g_inventory->registerPlayerPage(context, &registration, &page); r != inv::Result::Success) return fail("registerPlayerPage", int(r));
	if (const auto r = g_inventory->configurePlayerPage(context, page, &size); r != inv::Result::Success) return fail("configurePlayerPage", int(r));
	if (const auto r = g_panels->registerPanel(context, &panel, &g_panel); r != D2RL::Panels::Result::Success) return fail("registerPanel", int(r));
	const inv::PlayerPageItemPolicy policy {
		.structSize = inv::PlayerPageItemPolicySize, .page = page, .callback = OnItemPolicy};
	const D2RL::Panels::PlayerPageGridBinding binding {.structSize = D2RL::Panels::PlayerPageGridBindingSize,
	                                                   .panel = g_panel,
	                                                   .page = page,
	                                                   .childName = SlotName};
	if (const auto r = g_inventory->configurePlayerPageItemPolicy(context, &policy); r != inv::Result::Success) return fail("configurePlayerPageItemPolicy", int(r));
	if (const auto r = g_panels->bindPlayerPageGrid(context, &binding); r != D2RL::Panels::Result::Success) return fail("bindPlayerPageGrid", int(r));
	if (const auto r = events->registerUiMessageListener(context, &listener, &listenerHandle); r != D2RL::SharedEvents::Result::Success) return fail("registerUiMessageListener", int(r));
	if (const auto r = overlay->registerFrameCallback(context, &frame, &frameHandle); r != D2RL::Overlay::Result::Success) return fail("registerFrameCallback", int(r));
	std::snprintf(report, reportSize, "Sacrifice Altar panel: page %s, panel core-maps/%s, slot %s", PageId, PanelId, SlotName);
	return 1;
}

auto AltarCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "altar panel: %u opened, %u confirmed, %u amulets returned", g_opened.load(), g_confirmed.load(),
	              g_returned.load());
}
