// Stash tabs (#1553): Maps and Bossing tabs on a row above the stash's own tabs, the Maps tab laid out like Path of
// Exile's map tab and the Bossing tab like the RotW Materials tab, both round the Horadric Cube (common/stash_home.h has
// the page plan; the art is PANEL\pd2r\stash_maps_background and stash_bossing_background, after the RotW tabs').
//
// Each of our tabs is a TabBarWidget of two tabs whose first is hidden (off screen), so a click on it sends its
// tabMessages entry (PanelManager:OpenPanel:core-ui/StashTabMaps, ...), which names it, and a SelectTab the stash
// ignores: the stash takes its tab from its own BankTabs bar. So the frame after, the stock BankTabs is set to Shared
// through D2R's own tab-bar select (found by signature; no SDK service selects a tab, and the SDK's dispatchUiAction
// cannot carry the tab index the stash's SelectTab reads), which sends the stash's SelectTab as a click on Shared does.
// Then the stash's own page arrows (SharedStashLeft to page 1, where it stops, then SharedStashRight) open the tab's
// page. The other widgets are in bankexpansionlayouthd.json, all hidden; a frame watch shows the ones the state wants,
// because this loader's widget service only shows, hides and enables. Hidden buttons still take clicks on this loader
// (Slot2), so a hidden button is disabled too. Tier and base buttons (PanelManager:OpenPanel:core-ui/Stash..., caught
// and consumed like the padlock's) pick what the Maps tab shows: a base asks stash_affinity to swap its maps onto the
// Maps page. Both tabs show the stock cube (advancedstash_horadriccube, which the stash shows itself on its RotW
// tabs) at its RotW spot, and hides the stock page arrows while one of our tabs is up, so its page cannot change.
// D2R keeps the tab and page on show across a stash close, so the state stays until a stock tab or a new game.
#include "stash_tabs.h"

#include "log.h"
#include "sig.h"
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
constexpr int32_t LeftPresses = 60;  // from any page to page 1 (the left arrow stops there); covers 55 extra tabs
constexpr uint32_t PerFrame = 10;    // queued page arrows sent each frame

const D2RL::PluginContext* g_context = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;
const D2RL::OverlayService* g_overlay = nullptr;

// UI thread only
View g_view = View::None;
int32_t g_tier = 0;
int32_t g_base[3] = {0, 0, 0};  // the selected base of each tier
bool g_dispatching = false;
uint32_t g_frames = 0, g_opened = 0;  // the frame our tab was clicked or Shared selected for it
constexpr uint32_t OursFrames = 2;      // a SelectTab this soon after is that click's or that select's
const char* g_queue[LeftPresses + aff::LastReservedPage + 1];
uint32_t g_head = 0, g_tail = 0;
std::atomic<bool> g_reset {false};
std::atomic<uint32_t> g_clicks {0};

// D2R's tab bar and panel calls (93847 RVAs for reference only); any miss leaves the tabs working from Shared only
// 93847: 0x846170, void* (name): PanelManager::FetchPanel (as altar_panel.cpp)
sigscan::Entry FetchPanel {"PanelFetch", "stash", sigscan::Kind::Code, 0, 0, "48 8b d1 48 8b 0d ?? ?? ?? ?? 48 85 c9 0f 85 ?? ?? ?? ??"};
// 93847: 0x855c90, void* (widget, name): the named child widget, searched down the tree (as altar_panel.cpp)
sigscan::Entry FindChild {"WidgetFind", "stash", sigscan::Kind::Code, 0, 0,
                          "40 53 55 56 57 41 54 48 83 ec 20 4c 89 7c 24 68 48 8b f2 48 8b f9 e8 ?? ?? ?? ?? 44 0f b6 80 18 01 00 00"};
// 93847: 0x878fb0, void (tab bar, index, sound): TabBarWidget's select, which sends its onSwitchTabMessage with the
// index; read from its call in the bar's set-tab-enabled (0x8790d0), as another plugin may detour its entry
sigscan::Entry SelectTab {"TabBarSelect", "stash", sigscan::Kind::Rip, 15, 19,
                          "eb 03 45 8b ca 45 33 c0 41 8b d1 48 8b cb e8 ?? ?? ?? ?? 48 8b 5c 24 40 48 83 c4 20 5f c3"};
sigscan::Entry* const Sigs[] = {&FetchPanel, &FindChild, &SelectTab};
bool g_select = false;
bool g_wantShared = false;  // UI thread: our tab was clicked; the next frame selects the stock Shared tab

// The stock BankTabs to Shared (index 1), as a click on it: the stash gets its SelectTab and shows the Shared tab
void SelectShared() noexcept {
	void* panel = reinterpret_cast<void* (*)(const char*) noexcept>(FetchPanel.address)(Panel);
	void* bar = panel != nullptr ? reinterpret_cast<void* (*)(void*, const char*) noexcept>(FindChild.address)(panel, "BankTabs") : nullptr;
	if (bar == nullptr) {
		plog::Info("stash tabs: no BankTabs to select Shared on (panel %p)", panel);
		return;
	}
	g_opened = g_frames;
	const auto select = reinterpret_cast<void (*)(void*, int32_t, bool) noexcept>(SelectTab.address);
	select(bar, 1, false);
	for (const char* ours : {"pd2rMapsTabs", "pd2rBossingTabs"}) {  // back to the hidden tab: a bar ignores a click on its active tab
		if (void* own = reinterpret_cast<void* (*)(void*, const char*) noexcept>(FindChild.address)(panel, ours); own != nullptr) {
			select(own, 0, false);
		}
	}
}

// The stash, already on its Shared tab, to shared page `page` (0-based)
void OpenPage(uint32_t page) noexcept {
	g_head = g_tail = 0;
	for (int32_t i = 0; i < LeftPresses; ++i) {
		g_queue[g_tail++] = "SharedStashLeft";
	}
	for (uint32_t i = 0; i < page; ++i) {
		g_queue[g_tail++] = "SharedStashRight";
	}
}

void Send() noexcept {
	for (uint32_t n = 0; n < PerFrame && g_head < g_tail; ++n) {
		const D2RL::Widgets::UiAction action {.structSize = D2RL::Widgets::UiActionSize,
		                                      .target = "BankPanelMessage",
		                                      .command = g_queue[g_head++],
		                                      .text = ""};
		g_dispatching = true;
		const Result sent = g_widgets->dispatchUiAction(g_context, &action);
		g_dispatching = false;
		if (sent != Result::Success) {
			plog::Info("stash tabs: BankPanelMessage:%s not sent (%u)", action.command, static_cast<uint32_t>(sent));
			g_head = g_tail = 0;
			return;
		}
	}
}

void ShowTier(int32_t tier) noexcept {
	g_tier = tier;
	stash_affinity::ShowBase(aff::BaseCode(tier, g_base[tier]));
}

void Log(const char* what) noexcept {
	plog::Info("stash tabs: %s (view %u, tier %d, base %d)", what, static_cast<uint32_t>(g_view), g_tier + 1, g_base[g_tier]);
}

void Open(View view) noexcept {
	g_view = view;
	OpenPage(view == View::Maps ? aff::MapsPage : aff::BossingPage);
	if (view == View::Maps) {
		ShowTier(g_tier);
	}
	stash_affinity::SetTabOpen(true);
	Log(view == View::Maps ? "Maps" : "Bossing");
}

void Click(const char* what) noexcept {
	++g_clicks;
	int32_t t = 0, i = 0;
	if (g_view != View::Maps) {
		return;  // a tier or base button of a hidden Maps tab
	}
	if (sscanf_s(what, "Tier%d", &t) == 1 && t >= 1 && t <= 3) {
		ShowTier(t - 1);
	} else if (sscanf_s(what, "Base%d_%d", &t, &i) == 2 && t >= 1 && t <= 3 && aff::BaseCode(t - 1, i) != 0) {
		g_base[t - 1] = i;
		ShowTier(t - 1);
	} else {
		plog::Info("stash tabs: unknown command %s", what);
		return;
	}
	Log(what);
}

// The layout's widgets by name, found once: handles are paths the loader resolves on each call
struct Named {
	const char* name;
	WidgetHandle handle = D2RL::Widgets::InvalidHandle;
};
WidgetHandle g_panel = D2RL::Widgets::InvalidHandle;

auto Find(Named& w) noexcept -> WidgetHandle {
	if (g_panel != D2RL::Widgets::InvalidHandle && w.handle == D2RL::Widgets::InvalidHandle &&
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

// A button: hidden ones are disabled too, as they still take clicks
void ShowButton(Named& w, bool visible) noexcept {
	if (const WidgetHandle h = Find(w); h != D2RL::Widgets::InvalidHandle) {
		g_widgets->setWidgetVisible(g_context, h, visible);
		g_widgets->setWidgetEnabled(g_context, h, visible);
	}
}

Named g_background {"pd2rMapsBackground"}, g_bossingBackground {"pd2rBossingBackground"}, g_navigation {"StashNavigation"}, g_cube {"advancedstash_horadriccube"},
	g_cubeWithdraw {"withdraw_button"}, g_sharedOff {"pd2rSharedOff"}, g_mapsOn {"pd2rMapsTabsOn"}, g_mapsOnText {"pd2rMapsTabsOnText"},
	g_bossingOn {"pd2rBossingTabsOn"}, g_bossingOnText {"pd2rBossingTabsOnText"};
Named g_tiers[3][2] = {{{"pd2rTier1"}, {"pd2rTier1On"}}, {{"pd2rTier2"}, {"pd2rTier2On"}}, {{"pd2rTier3"}, {"pd2rTier3On"}}};
struct BaseWidgets {
	char names[3][24];
	Named button, frame, box;
};
BaseWidgets g_bases[3][aff::PageWidth];

void InitNames() noexcept {
	for (int t = 0; t < 3; ++t) {
		for (int i = 0; i < aff::PageWidth; ++i) {
			BaseWidgets& b = g_bases[t][i];
			std::snprintf(b.names[0], sizeof b.names[0], "pd2rBase%d_%d", t + 1, i);
			std::snprintf(b.names[1], sizeof b.names[1], "pd2rBaseSel%d_%d", t + 1, i);
			std::snprintf(b.names[2], sizeof b.names[2], "pd2rBaseBox%d_%d", t + 1, i);
			b.button.name = b.names[0];
			b.frame.name = b.names[1];
			b.box.name = b.names[2];
		}
	}
}

// Back to a stock tab, before the stash handles its SelectTab: the stock widgets ours hid are shown, and the stash's own
// handling of the message then hides the ones its tab does not use
void Leave() noexcept {
	g_view = View::None;
	g_head = g_tail = 0;
	stash_affinity::SetTabOpen(false);
	Show(g_navigation, true);
	Show(g_cube, false);
}

auto OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
	-> D2RL::SharedEvents::UiMessageAction {
	using Action = D2RL::SharedEvents::UiMessageAction;
	if (event == nullptr || event->target == nullptr || event->command == nullptr) {
		return Action::Continue;
	}
	const char* text = event->text != nullptr ? event->text : "";
	if (std::strcmp(event->target, "BankPanelMessage") == 0) {
		if (g_dispatching || std::strcmp(event->command, "SelectTab") != 0) {
			return Action::Continue;
		}
		if (g_view != View::None && g_frames - g_opened > OursFrames) {  // not our tab's own SelectTab, nor our select's
			Leave();
			Log("stock tab");
		}
		return Action::Continue;  // the stash selects the tab (ours: Shared)
	}
	if (std::strcmp(event->target, "PanelManager") != 0 || std::strcmp(event->command, "OpenPanel") != 0 ||
	    std::strncmp(text, Prefix, sizeof Prefix - 1) != 0) {
		return Action::Continue;
	}
	const char* what = text + sizeof Prefix - 1;
	if (std::strcmp(what, "TabMaps") == 0 || std::strcmp(what, "TabBossing") == 0) {
		Open(what[3] == 'M' ? View::Maps : View::Bossing);
		g_opened = g_frames;
		g_wantShared = g_select;
	} else if (std::strcmp(what, "Shared") == 0) {  // the stock Shared tab, already selected under ours: its first page
		Leave();
		OpenPage(0);
		Log("Shared");
	} else if (std::strcmp(what, "Hidden") != 0) {  // Hidden: our bars' off-screen tab 0 (an empty message asserts, #1649)
		Click(what);
	}
	return Action::Consume;
}

// UI thread
void OnFrame(const D2RL::PluginContext*, const D2RL::Overlay::Frame*, void*) noexcept {
	++g_frames;
	if (g_reset.exchange(false)) {
		g_view = View::None;
		g_head = g_tail = 0;
		stash_affinity::ShowBase(0);
		stash_affinity::SetTabOpen(false);
	}
	if (g_panel == D2RL::Widgets::InvalidHandle && g_widgets->findPanel(g_context, Panel, &g_panel) != Result::Success) {
		g_panel = D2RL::Widgets::InvalidHandle;
		return;
	}
	if (g_wantShared) {
		g_wantShared = false;
		SelectShared();
	}
	if (g_frames - g_opened > OursFrames) {
		Send();  // the page arrows, once the stash is on its Shared tab (it reads them there only)
	}
	const bool ours = g_view != View::None, maps = g_view == View::Maps;
	Show(g_mapsOn, maps);
	Show(g_mapsOnText, maps);
	Show(g_bossingOn, g_view == View::Bossing);
	Show(g_bossingOnText, g_view == View::Bossing);
	Show(g_background, maps);
	Show(g_bossingBackground, g_view == View::Bossing);
	ShowButton(g_sharedOff, ours);  // the stock Shared tab, lit under ours, drawn unlit: one tab looks selected
	if (ours) {
		Show(g_navigation, false);  // the page stays ours
	}
	if (ours) {
		Show(g_cube, true);
		Show(g_cubeWithdraw, false);  // the RotW tabs' Withdraw, not ours
	}
	for (int t = 0; t < 3; ++t) {
		ShowButton(g_tiers[t][0], maps && g_tier != t);
		ShowButton(g_tiers[t][1], maps && g_tier == t);
		for (int i = 0; i < aff::PageWidth; ++i) {
			if (aff::BaseCode(t, i) == 0) {
				continue;
			}
			BaseWidgets& b = g_bases[t][i];
			const bool shown = maps && g_tier == t;
			Show(b.box, shown);
			ShowButton(b.button, shown);
			Show(b.frame, shown && g_base[t] == i);
		}
	}
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
	sigscan::Resolve(Sigs, std::size(Sigs), context->exeBase, nullptr, 0);
	g_select = true;
	for (const sigscan::Entry* e : Sigs) {
		if (e->state != sigscan::State::Ok) {
			g_select = false;
			plog::Info("stash tabs: signature %s %s", e->name, sigscan::StateText(e->state));
		}
	}
	InitNames();
	const D2RL::LifecycleService* lifecycle = nullptr;
	const D2RL::SharedEventService* events = nullptr;
	if (!(Query(g_widgets, D2RL::HasWidgetServiceField, D2RL::WidgetServiceRequiredSize) &
	      Query(lifecycle, D2RL::HasLifecycleServiceField, D2RL::LifecycleServiceRequiredSize) &
	      Query(events, D2RL::HasSharedEventServiceField, D2RL::SharedEventServiceRequiredSize) &
	      Query(g_overlay, D2RL::HasOverlayServiceField, D2RL::OverlayServiceRequiredSize))) {
		std::snprintf(report, size, "FAILED (a widget, lifecycle, event or overlay service is missing)");
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
	    g_overlay->registerFrameCallback(context, &frame, &frameHandle) != D2RL::Overlay::Result::Success) {
		std::snprintf(report, size, "FAILED (a listener was refused)");
		return;
	}
	std::snprintf(report, size, "Maps and Bossing stash tabs on a row above the stock tabs; Shared tab select %s",
	              g_select ? "by signature" : "off (signature not found: the tabs work from the Shared tab only)");
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "stash tabs: view %u, tier %d, %u clicks", static_cast<uint32_t>(g_view), g_tier + 1,
	              g_clicks.load());
}

}  // namespace stash_tabs
