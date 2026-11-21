// Stash tabs (#1553): a second page of stash tabs (Maps, Bossing) behind arrows at both ends of the tab row, and the
// Maps tab laid out like Path of Exile's map tab round the Horadric Cube (common/stash_home.h has the page plan).
// The widgets are in bankexpansionlayouthd.json, all hidden; a frame watch shows the ones the state wants, because
// this loader's widget service only shows, hides and enables. Their clicks (PanelManager:OpenPanel:core-ui/Stash...,
// caught and consumed like the padlock's) change the state; a tab or tier opens its shared page with the stash's own
// messages (BankPanelMessage:SelectTab to the Shared tab, then SharedStashLeft to page 1, which clamps there, and
// SharedStashRight to the page), and a base button asks stash_affinity to swap that base's maps onto the Maps page.
// The Maps tab shows the stock cube (advancedstash_horadriccube, which the stash shows itself on its RotW tabs) at its
// RotW spot, and hides the stock page arrows while one of our tabs is up, so its page cannot change under it.
//
// shortcut: the base buttons show their map counts only with a widget service that has setText (D2RLoader 1.4.0);
// 1.3.1 shows the icons and the base's name on hover.
// shortcut: the stash panel's open is not seen; the state stays from the last open in the same game.
#include "stash_tabs.h"

#include "log.h"
#include "stash_affinity.h"
#include "stash_home.h"

#include <D2RLPlugin/lifecycle.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/overlay.h>
#include <D2RLPlugin/shared_events.h>
#include <D2RLPlugin/widgets.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace stash_tabs {

namespace {

namespace aff = pd2::affinity;
using D2RL::Widgets::WidgetHandle;
using D2RL::Widgets::Result;

enum class View : uint8_t { None, Maps, Bossing };

constexpr char Panel[] = "BankExpansionLayout";
constexpr char Prefix[] = "core-ui/Stash";
constexpr int32_t SharedTab = 1;     // BankTabs: personal, shared, gems, materials, runes
constexpr int32_t LeftPresses = 19;  // from any page to page 1 (the left arrow stops there)
constexpr uint32_t PerFrame = 8;     // queued stash messages sent each frame
constexpr uint32_t CountFrames = 30;

const D2RL::PluginContext* g_context = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;
bool g_setText = false;

// UI thread only
int32_t g_row = 0;  // 0: the stock tabs, 1: ours
View g_view = View::None;
int32_t g_tier = 0;
int32_t g_base[3] = {0, 0, 0};  // the selected base of each tier
bool g_dispatching = false;
uint32_t g_frames = 0;
std::atomic<bool> g_reset {false};
std::atomic<uint32_t> g_clicks {0}, g_logged {0};

struct Message {
	const char* command;
	char text[8];
};
Message g_queue[64];
uint32_t g_head = 0, g_tail = 0;

void Queue(const char* command, const char* text = "") noexcept {
	if (g_tail >= std::size(g_queue)) {
		return;
	}
	Message& m = g_queue[g_tail++];
	m.command = command;
	std::snprintf(m.text, sizeof m.text, "%s", text);
}

// The stash on shared page `page` (0-based)
void OpenPage(uint32_t page) noexcept {
	g_head = g_tail = 0;
	char tab[8];
	std::snprintf(tab, sizeof tab, "%d", SharedTab);
	Queue("SelectTab", tab);
	for (int32_t i = 0; i < LeftPresses; ++i) {
		Queue("SharedStashLeft");
	}
	for (uint32_t i = 0; i < page; ++i) {
		Queue("SharedStashRight");
	}
}

void Send() noexcept {
	for (uint32_t n = 0; n < PerFrame && g_head < g_tail; ++n) {
		const Message& m = g_queue[g_head++];
		const D2RL::Widgets::UiAction action {.structSize = D2RL::Widgets::UiActionSize,
		                                      .target = "BankPanelMessage",
		                                      .command = m.command,
		                                      .text = m.text};
		g_dispatching = true;
		const Result sent = g_widgets->dispatchUiAction(g_context, &action);
		g_dispatching = false;
		if (sent != Result::Success) {
			plog::Info("stash tabs: BankPanelMessage:%s:%s not sent (%u)", m.command, m.text, static_cast<uint32_t>(sent));
			g_head = g_tail = 0;
			return;
		}
	}
}

void ShowTier(int32_t tier) noexcept {
	g_tier = tier;
	stash_affinity::ShowBase(aff::BaseCode(tier, g_base[tier]));
}

void Click(const char* what) noexcept {
	++g_clicks;
	int32_t t = 0, i = 0;
	if (std::strcmp(what, "TabsPrev") == 0) {
		g_row = 0;
	} else if (std::strcmp(what, "TabsNext") == 0) {
		g_row = 1;
	} else if (std::strcmp(what, "Maps") == 0) {
		g_view = View::Maps;
		OpenPage(aff::MapsPage);
		ShowTier(g_tier);
	} else if (std::strcmp(what, "Bossing") == 0) {
		g_view = View::Bossing;
		OpenPage(aff::BossingPage);
	} else if (sscanf_s(what, "Tier%d", &t) == 1 && t >= 1 && t <= 3) {
		ShowTier(t - 1);
	} else if (sscanf_s(what, "Base%d_%d", &t, &i) == 2 && t >= 1 && t <= 3 && aff::BaseCode(t - 1, i) != 0) {
		g_base[t - 1] = i;
		ShowTier(t - 1);
	} else {
		plog::Info("stash tabs: unknown command %s", what);
		return;
	}
	stash_affinity::SetMapsOpen(g_view == View::Maps);
	plog::Info("stash tabs: %s (row %d, view %u, tier %d, base %d)", what, g_row, static_cast<uint32_t>(g_view), g_tier + 1,
	           g_base[g_tier]);
}

auto OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
	-> D2RL::SharedEvents::UiMessageAction {
	using Action = D2RL::SharedEvents::UiMessageAction;
	if (event == nullptr || event->target == nullptr || event->command == nullptr) {
		return Action::Continue;
	}
	const char* text = event->text != nullptr ? event->text : "";
	if (std::strcmp(event->target, "BankPanelMessage") == 0) {
		if (g_logged.fetch_add(1) < 40) {
			plog::Info("stash tabs: saw BankPanelMessage:%s:%s%s", event->command, text, g_dispatching ? " (ours)" : "");
		}
		if (!g_dispatching && std::strcmp(event->command, "SelectTab") == 0 && g_view != View::None) {
			g_view = View::None;  // a stock tab: the stash shows its own widgets for it after this message
			g_head = g_tail = 0;
			stash_affinity::SetMapsOpen(false);
		}
		return Action::Continue;
	}
	if (std::strcmp(event->target, "PanelManager") != 0 || std::strcmp(event->command, "OpenPanel") != 0 ||
	    std::strncmp(text, Prefix, sizeof Prefix - 1) != 0) {
		return Action::Continue;
	}
	Click(text + sizeof Prefix - 1);
	return Action::Consume;
}

// The layout's widgets by name, found once: handles are paths the loader resolves on each call
struct Named {
	const char* name;
	WidgetHandle handle = D2RL::Widgets::InvalidHandle;
};
WidgetHandle g_panel = D2RL::Widgets::InvalidHandle;

auto Find(Named& w) noexcept -> WidgetHandle {
	if (w.handle == D2RL::Widgets::InvalidHandle &&
	    g_widgets->findWidget(g_context, g_panel, w.name, &w.handle) != Result::Success) {
		w.handle = D2RL::Widgets::InvalidHandle;
	}
	return w.handle;
}

void Show(Named& w, bool visible) noexcept {
	if (const WidgetHandle h = Find(w); h != D2RL::Widgets::InvalidHandle) {
		g_widgets->setWidgetVisible(g_context, h, visible);
	}
}

Named g_bankTabs {"BankTabs"}, g_navigation {"StashNavigation"}, g_cube {"advancedstash_horadriccube"},
	g_cubeWithdraw {"withdraw_button"}, g_left {"pd2rTabsLeft"}, g_right {"pd2rTabsRight"}, g_maps {"pd2rTabMaps"},
	g_mapsOn {"pd2rTabMapsOn"}, g_bossing {"pd2rTabBossing"}, g_bossingOn {"pd2rTabBossingOn"};
Named g_tiers[3][2] = {{{"pd2rTier1"}, {"pd2rTier1On"}}, {{"pd2rTier2"}, {"pd2rTier2On"}}, {{"pd2rTier3"}, {"pd2rTier3On"}}};
struct BaseWidgets {
	char names[3][24];
	Named button, frame, count;
};
BaseWidgets g_bases[3][aff::PageWidth];
View g_drawn = View::None;  // the view the last frame drew

void InitNames() noexcept {
	for (int t = 0; t < 3; ++t) {
		for (int i = 0; i < aff::PageWidth; ++i) {
			BaseWidgets& b = g_bases[t][i];
			std::snprintf(b.names[0], sizeof b.names[0], "pd2rBase%d_%d", t + 1, i);
			std::snprintf(b.names[1], sizeof b.names[1], "pd2rBaseSel%d_%d", t + 1, i);
			std::snprintf(b.names[2], sizeof b.names[2], "pd2rBaseCount%d_%d", t + 1, i);
			b.button.name = b.names[0];
			b.frame.name = b.names[1];
			b.count.name = b.names[2];
		}
	}
}

// UI thread
void OnFrame(const D2RL::PluginContext*, const D2RL::Overlay::Frame*, void*) noexcept {
	++g_frames;
	if (g_reset.exchange(false)) {
		g_row = 0;
		g_view = g_drawn = View::None;
		g_head = g_tail = 0;
		stash_affinity::ShowBase(0);
		stash_affinity::SetMapsOpen(false);
	}
	if (g_panel == D2RL::Widgets::InvalidHandle && g_widgets->findPanel(g_context, Panel, &g_panel) != Result::Success) {
		g_panel = D2RL::Widgets::InvalidHandle;
		return;
	}
	Send();
	const bool ours = g_view != View::None, maps = g_view == View::Maps;
	Show(g_bankTabs, g_row == 0);
	if (const WidgetHandle h = Find(g_left); h != D2RL::Widgets::InvalidHandle) {
		g_widgets->setWidgetVisible(g_context, h, true);
		g_widgets->setWidgetEnabled(g_context, h, g_row == 1);
	}
	if (const WidgetHandle h = Find(g_right); h != D2RL::Widgets::InvalidHandle) {
		g_widgets->setWidgetVisible(g_context, h, true);
		g_widgets->setWidgetEnabled(g_context, h, g_row == 0);
	}
	Show(g_maps, g_row == 1 && !maps);
	Show(g_mapsOn, g_row == 1 && maps);
	Show(g_bossing, g_row == 1 && g_view != View::Bossing);
	Show(g_bossingOn, g_row == 1 && g_view == View::Bossing);
	if (ours) {
		Show(g_navigation, false);  // the page stays ours
	} else if (g_drawn != View::None) {
		Show(g_navigation, true);  // back to a stock tab: the stash's own SelectTab handling hides it again where it should
	}
	if (maps) {
		Show(g_cube, true);
		Show(g_cubeWithdraw, false);  // the RotW tabs' Withdraw, not ours
	} else if (g_drawn == View::Maps && g_view == View::Bossing) {
		Show(g_cube, false);
	}
	const bool count = g_setText && g_frames % CountFrames == 0;
	for (int t = 0; t < 3; ++t) {
		Show(g_tiers[t][0], maps && g_tier != t);
		Show(g_tiers[t][1], maps && g_tier == t);
		for (int i = 0; i < aff::PageWidth; ++i) {
			if (aff::BaseCode(t, i) == 0) {
				continue;
			}
			BaseWidgets& b = g_bases[t][i];
			const bool shown = maps && g_tier == t;
			Show(b.button, shown);
			Show(b.frame, shown && g_base[t] == i);
			Show(b.count, shown && g_setText);
			if (shown && count) {
				char text[16];
				std::snprintf(text, sizeof text, "%u", stash_affinity::MapCount(t, i));
				if (const WidgetHandle h = Find(b.count); h != D2RL::Widgets::InvalidHandle) {
					g_widgets->setText(g_context, h, text);
				}
			}
		}
	}
	g_drawn = g_view;
}

void OnPlayerReady(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	g_reset.store(true);  // the UI thread starts the new game on the stock tabs
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
	InitNames();
	const D2RL::LifecycleService* lifecycle = nullptr;
	const D2RL::SharedEventService* events = nullptr;
	const D2RL::OverlayService* overlay = nullptr;
	if (!(Query(g_widgets, D2RL::HasWidgetServiceField, D2RL::WidgetServiceRequiredSize) &
	      Query(lifecycle, D2RL::HasLifecycleServiceField, D2RL::LifecycleServiceRequiredSize) &
	      Query(events, D2RL::HasSharedEventServiceField, D2RL::SharedEventServiceRequiredSize) &
	      Query(overlay, D2RL::HasOverlayServiceField, D2RL::OverlayServiceRequiredSize))) {
		std::snprintf(report, size, "FAILED (a widget, lifecycle, event or overlay service is missing)");
		return;
	}
	g_setText = g_widgets->serviceSize >= D2RL::WidgetServiceSetTextFieldEnd && g_widgets->setText != nullptr;
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
		std::snprintf(report, size, "FAILED (a listener was refused)");
		return;
	}
	std::snprintf(report, size, "a second stash tab row with Maps (tiers, bases, the cube) and Bossing; map counts %s",
	              g_setText ? "on" : "off (no setText in this loader)");
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "stash tabs: row %d, view %u, tier %d, %u clicks", g_row, static_cast<uint32_t>(g_view),
	              g_tier + 1, g_clicks.load());
}

}  // namespace stash_tabs
