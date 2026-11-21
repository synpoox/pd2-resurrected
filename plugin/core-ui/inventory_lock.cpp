// Lock Inventory (#1578). The padlock is a ToggleButtonWidget in both HD inventory layouts; its click message
// (PanelManager:OpenPanel:core-ui/InventoryLock, caught and consumed like the altar's confirm) flips the lock, and a frame
// watch keeps the button drawn as the lock is, across panels and games. PD2 keeps the lock in its settings and sends it
// to the server (ProjectDiablo.dll 0x102d15c0, packet 0x5c), whose wrappers of the body-slot handlers refuse while it is
// on (0x10394bd0 and others) and play the "I can't" sound.
//
// Native: no SDK service sees a body-slot change. ItemInteractionService skips equipment and cursor paths, the
// inventory item policies apply to custom pages only, and InputService sees bindings, not slot clicks. So the server's
// client-message dispatch (PlrMsg, 93847 0x4f2fa0, by signature) drops the messages that change a body slot
// (ChangesEquipment) from the local player while locked; clicks, Ctrl/Shift clicks and the pad all send those. The
// client marks the item pending and its timer clears it, as for any refused move.
//
// shortcut: the lock lives in memory for this run of the game (all characters), not in a setting; upgrade if players
// want it kept across restarts.
// shortcut: single player and the host only (a TCP/IP client runs no server here, so its padlock does nothing);
// upgrade with a plugin message to the host if online play needs it.
// shortcut: no "I can't" sound on a refused move, and PD2's lock of magic+ charms in the inventory is not ported.
#include "inventory_lock.h"

#include "log.h"
#include "pd2_engine.h"
#include "sig_hook.h"

#include <D2RLPlugin/lifecycle.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/overlay.h>
#include <D2RLPlugin/shared_events.h>
#include <D2RLPlugin/widgets.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace inventory_lock {

namespace {

constexpr uint8_t Equip[] = {0x1a, 0, 0, 0, 0, 4, 0, 0, 0};
constexpr uint8_t Merc[] = {0x58, 1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t GridList[] = {0x22, 0xff, 0xff, 0xff, 0xff, 7, 0, 0, 0, 0};
constexpr uint8_t BodyList[] = {0x22, 7, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0};
constexpr auto ListOf(uint8_t mode) noexcept {
	struct { uint8_t b[0x20] {}; } p;
	p.b[0] = 0x60;
	p.b[0x15] = mode;
	return p;
}
constexpr uint8_t UseItem[] = {0x26, 0, 0, 0, 0};
constexpr uint8_t Insert[] = {0x18, 0, 0, 0, 0};
static_assert(ChangesEquipment(Equip, sizeof Equip));
static_assert(ChangesEquipment(Merc, sizeof Merc));
static_assert(!ChangesEquipment(GridList, sizeof GridList));  // a grid's list move: not a body slot
static_assert(ChangesEquipment(BodyList, sizeof BodyList));
static_assert(ChangesEquipment(ListOf(1).b, 0x20) && ChangesEquipment(ListOf(2).b, 0x20));
static_assert(!ChangesEquipment(ListOf(0).b, 0x20) && !ChangesEquipment(ListOf(3).b, 0x20));
static_assert(!ChangesEquipment(ListOf(1).b, 0x15));  // too short to hold the mode
static_assert(!ChangesEquipment(UseItem, sizeof UseItem) && !ChangesEquipment(Insert, sizeof Insert));
static_assert(!ChangesEquipment(nullptr, 9) && !ChangesEquipment(Equip, 0));

constexpr char Command[] = "core-ui/InventoryLock";
constexpr char Button[] = "InventoryLock";
constexpr const char* Panels[] = {"PlayerInventoryExpansionLayout", "PlayerInventoryOriginalLayout"};
constexpr uint32_t WatchFrames = 10;

const D2RL::PluginContext* g_context = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;
std::atomic<bool> g_locked {false};
std::atomic<uint32_t> g_localId {0xffffffffU};
std::atomic<uint32_t> g_refused {0}, g_toggles {0};
uint32_t g_frames = 0;

// 93847 0x4f2fa0, int (game, player, message, size): the server's client-message dispatch (PlrMsg handler table by id)
using DispatchFn = int (*)(void*, void*, const uint8_t*, int) noexcept;
DispatchFn g_dispatch = nullptr;
constexpr uint8_t DispatchEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20};

auto OnMessage(void* game, void* player, const uint8_t* message, int size) noexcept -> int {
	if (g_locked.load(std::memory_order_relaxed) && player != nullptr && size > 0 &&
	    d2r::At<uint32_t>(player, d2r::rva::UnitIdField) == g_localId.load(std::memory_order_relaxed) &&
	    ChangesEquipment(message, static_cast<size_t>(size))) {
		++g_refused;
		plog::Info("inventory lock: refused message 0x%02x (%d bytes)", message[0], size);
		return 0;
	}
	return g_dispatch(game, player, message, size);
}

auto OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
	-> D2RL::SharedEvents::UiMessageAction {
	if (event == nullptr || event->target == nullptr || event->command == nullptr || event->text == nullptr ||
	    std::strcmp(event->target, "PanelManager") != 0 || std::strcmp(event->command, "OpenPanel") != 0 ||
	    std::strcmp(event->text, Command) != 0) {
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}
	const bool locked = !g_locked.load();
	g_locked.store(locked);
	++g_toggles;
	plog::Info("inventory lock: %s (player %u)", locked ? "locked" : "unlocked", g_localId.load());
	return D2RL::SharedEvents::UiMessageAction::Consume;
}

// UI thread: the button shows the lock (a new game's panel starts unlocked)
void OnFrame(const D2RL::PluginContext*, const D2RL::Overlay::Frame*, void*) noexcept {
	if (++g_frames % WatchFrames != 0) {
		return;
	}
	const bool locked = g_locked.load();
	for (const char* name : Panels) {
		D2RL::Widgets::WidgetHandle panel {}, button {};
		bool shown = false;
		if (g_widgets->findPanel(g_context, name, &panel) == D2RL::Widgets::Result::Success &&
		    g_widgets->findWidget(g_context, panel, Button, &button) == D2RL::Widgets::Result::Success &&
		    g_widgets->getToggleState(g_context, button, &shown) == D2RL::Widgets::Result::Success && shown != locked) {
			g_widgets->setToggleState(g_context, button, locked);
		}
	}
}

void OnPlayerReady(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	if (event != nullptr) {
		g_localId.store(event->playerId);
	}
}

template <class Service>
auto Query(const Service*& service, bool (*has)(const Service*, uint32_t), uint32_t required) noexcept -> bool {
	if (g_context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !has(service, required)) {
		service = nullptr;
	}
	return service != nullptr;
}

}  // namespace

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	g_context = context;
	const D2RL::LifecycleService* lifecycle = nullptr;
	const D2RL::SharedEventService* events = nullptr;
	const D2RL::OverlayService* overlay = nullptr;
	if (!(Query(g_widgets, D2RL::HasWidgetServiceField, D2RL::WidgetServiceSetToggleStateFieldEnd) &
	      Query(lifecycle, D2RL::HasLifecycleServiceField, D2RL::LifecycleServiceRequiredSize) &
	      Query(events, D2RL::HasSharedEventServiceField, D2RL::SharedEventServiceRequiredSize) &
	      Query(overlay, D2RL::HasOverlayServiceField, D2RL::OverlayServiceRequiredSize))) {
		std::snprintf(report, size, "FAILED (a widget, lifecycle, event or overlay service is missing)");
		return;
	}
	if (!sigscan::HookBySignature(context, "PlrMsgDispatch",
	                              "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 83 ec 20 "
	                              "41 0f b6 30 45 8b e1 4d 8b f8 48 8b fa 4c 8b e9 bb 01 00 00 00 40 80 fe 67",
	                              0, DispatchEntry, sizeof DispatchEntry, OnMessage, &g_dispatch)) {
		std::snprintf(report, size, "FAILED (the server message dispatch: signature or bytes differ)");
		return;
	}
	const D2RL::Lifecycle::GameplayEventListener ready {.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
	                                                    .kind = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
	                                                    .callback = OnPlayerReady};
	const D2RL::SharedEvents::UiMessageListener listener {.structSize = D2RL::SharedEvents::UiMessageListenerSize,
	                                                      .callback = OnUiMessage};
	const D2RL::Overlay::CallbackRegistration frame {.structSize = D2RL::Overlay::CallbackRegistrationSize,
	                                                 .phase = D2RL::Overlay::Phase::AfterGameUi,
	                                                 .callback = OnFrame};
	D2RL::Lifecycle::ListenerHandle readyHandle = D2RL::Lifecycle::InvalidHandle;
	D2RL::SharedEvents::ListenerHandle listenerHandle = D2RL::SharedEvents::InvalidHandle;
	D2RL::Overlay::CallbackHandle frameHandle = D2RL::Overlay::InvalidCallbackHandle;
	if (lifecycle->registerGameplayEventListener(context, &ready, &readyHandle) != D2RL::Lifecycle::Result::Success ||
	    events->registerUiMessageListener(context, &listener, &listenerHandle) != D2RL::SharedEvents::Result::Success ||
	    overlay->registerFrameCallback(context, &frame, &frameHandle) != D2RL::Overlay::Result::Success) {
		std::snprintf(report, size, "FAILED (a listener was refused; the hook passes everything while unlocked)");
		return;
	}
	std::snprintf(report, size, "the inventory padlock locks the player's and the mercenary's body slots");
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "inventory lock: %s, %u toggles, %u moves refused", g_locked.load() ? "locked" : "unlocked",
	              g_toggles.load(), g_refused.load());
}

}  // namespace inventory_lock
