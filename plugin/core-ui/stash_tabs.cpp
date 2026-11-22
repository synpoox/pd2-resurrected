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
// tabs) at its RotW spot, shown and enabled as the stash does there (#1699), and hides the stock page
// arrows while one of our tabs is up, so its page cannot change.
// D2R keeps the tab and page on show across a stash close, so the state stays until a stock tab or a new game.
// The stock Shared tab never pages onto the reserved pages (#1653): no SDK service reads the stash page and this
// loader's widget service has no getText for its label, so the page is counted from the arrows (ours and the player's),
// and a player's Right arrow that would reach a reserved page is consumed; leaving our tab pages the stash back to page 1.
// On the Maps tab the pad's LB/RB step the base and its left stick click the tier (#1669), as the buttons' clicks do.
// The pad (#1661, #1671) does the same, caught as its actions in the stash's OnMessage (any binding, pad or pad type): its
// page right (LB+RT) is counted and consumed at page 15, and its tab switch past Runes or Personal opens our tabs (the
// controller layout's pd2rPadTabs bar shows them lit; BankTabs stays on Shared, drawn unlit).
#include "stash_tabs.h"

#include "log.h"
#include "sig.h"
#include "stash_affinity.h"
#include "stash_home.h"
#include "stash_toml.h"

#include <D2RLPlugin/lifecycle.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/overlay.h>
#include <D2RLPlugin/shared_events.h>
#include <D2RLPlugin/widgets.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>

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
uint32_t g_sendFails = 0;     // consecutive frames the queue's next arrow was not sent
bool g_pending = false;       // our tab is up but its page arrows have not all been sent: the shown page is unknown
std::atomic<bool> g_inGame {false};  // a player is in a game: the notice counts down from there, not on the menu
const char* g_notice = nullptr;  // an in-game line about the stash tabs (#1808), set at install
bool g_noticeLogged = false;
float g_noticeLeft = 40.0f;      // seconds it still shows
uint32_t g_pages = UINT32_MAX;  // shared pages the stash has (3 + add_shared_tabs: the fewest any mode has, Classic/LoD; RotW has 5), from d2rloader.toml; unknown: no limit
constexpr uint32_t MaxSendFails = 30;
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
// 93847: 0x8460d0, bool (): the message being dispatched is already handled (as altar_panel.cpp)
sigscan::Entry MessageHandled {"MessageHandled", "stash", sigscan::Kind::Code, 0, 0,
                               "48 8b 05 ?? ?? ?? ?? 48 85 c0 74 08 0f b6 80 b8 00 00 00 c3 c3"};
// 93847: 0x843fc0, void (): marks the message being dispatched handled (as altar_panel.cpp)
sigscan::Entry MarkHandled {"MessageMarkHandled", "stash", sigscan::Kind::Code, 0, 0,
                            "48 8b 05 ?? ?? ?? ?? 48 85 c0 74 07 c6 80 b8 00 00 00 01 c3"};
sigscan::Entry* const Sigs[] = {&FetchPanel, &FindChild, &SelectTab, &MessageHandled, &MarkHandled};
bool g_select = false;
// UI thread: the shared page on show (0-based), counted from the arrows; a new game starts on page 0.
// A tab's page is published to stash_affinity (ViewPage) only once every arrow was sent (g_pending); until then, and after
// MaxSendFails failed frames, it is unknown (0: no drop filter). Our tab is refused when the stash has too few pages (g_pages).
// shortcut: a page change that sends no arrow (another plugin's jump) is not counted, and the general pages after the
// reserved ones (add_shared_tabs above 17) are out of the Shared tab's reach; read the page if the SDK gains a read.
uint32_t g_page = 0;
bool g_wantShared = false;  // UI thread: our tab was clicked; the next frame selects the stock Shared tab
// Pad layout (#1661): the stock BankTabs has our Maps and Bossing tabs after its five, but the stash reads its tab from
// that bar's index wherever it refreshes and asserts, then crashes, past its five; so the bar never stays on ours. LT/RT
// onto ours is caught (below) and opens it as a mouse click does, with the bar on Shared, and pd2rPadTabs (our two tabs,
// a bar the stash does not read) is shown over it on our tab.
// UI thread: the stock tab named by the pad bar's SelectTab (0-4; the mouse's bar names none: -1), our pad tab (5 or 6)
int32_t g_tab = 0, g_padTab = 0;
bool g_stashOpen = false;           // UI thread
constexpr const char* TabNames[] = {"Personal", "Shared", "Gems", "Materials", "Runes"};

// The player's page step on the stock Shared tab (`how` names a right step); false: it would reach a reserved page
auto PlayerPage(bool right, const char* how) noexcept -> bool {
	if (!right) {
		g_page -= g_page > 0 ? 1 : 0;
		return true;
	}
	if (g_page + 1 >= aff::FirstReservedPage) {  // the reserved pages are the Maps and Bossing tabs'
		plog::Info("stash tabs: Shared %s at page %u stops (pages %u-%u are reserved)", how, g_page + 1, aff::FirstReservedPage + 1,
		           aff::LastReservedPage + 1);
		return false;
	}
	++g_page;
	return true;
}

// #1808: the mod's d2rloader.toml lost its live add_shared_tabs (a rewritten template, a kept old file): put 17 back in it (#1854: 3 stock pages + 17 reach Bossing for Classic/LoD), the
// loader reads it at game start. A .bak of the file is kept once; the write is a temp file moved over it. Never the game-wide file.
auto RestoreExtraTabs(const std::wstring& path) noexcept -> bool {
	std::string text;
	{
		FILE* in = nullptr;
		if (_wfopen_s(&in, path.c_str(), L"rb") != 0 || in == nullptr) {
			return false;
		}
		char buf[4096];
		for (size_t n; (n = std::fread(buf, 1, sizeof buf, in)) > 0;) {
			text.append(buf, n);
		}
		std::fclose(in);
	}
	const std::string fixed = pd2::stash_toml::WithExtraTabs(text, static_cast<int32_t>(aff::RequiredExtraTabs));
	if (fixed.empty()) {
		return false;  // a stash section we cannot place a key in safely: the player fixes it by hand
	}
	CopyFileW(path.c_str(), (path + L".bak").c_str(), TRUE);  // fails when a .bak is already there: the first one stays
	const std::wstring temp = path + L".tmp";
	FILE* out = nullptr;
	if (_wfopen_s(&out, temp.c_str(), L"wb") != 0 || out == nullptr) {
		return false;
	}
	const bool wrote = std::fwrite(fixed.data(), 1, fixed.size(), out) == fixed.size();
	if (std::fclose(out) != 0 || !wrote || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		DeleteFileW(temp.c_str());
		return false;
	}
	return true;
}

// The stash's shared pages: 3 + add_shared_tabs (Classic/LoD stock, the fewest of any mode; RotW has 5) of the mod's d2rloader.toml (beside the plugin's own config), else of the game-wide
// <game>/d2rloader/config/d2rloader.toml (the mod's value wins); neither set: the loader default 0; no file readable: no limit
auto ReadPages(const wchar_t* configPath) noexcept -> uint32_t {
	if (configPath == nullptr) {
		plog::Info("stash tabs: no config path: the stash page count is unknown, no tab limit");
		return UINT32_MAX;
	}
	std::wstring dir = configPath;
	const size_t slash = dir.find_last_of(L"\\/");
	dir = slash == std::wstring::npos ? L"" : dir.substr(0, slash + 1);
	std::wstring game = dir;  // <game>/mods/<mod>/d2rloader/config/ up four folders: <game>/
	for (int i = 0; i < 4 && !game.empty(); ++i) {
		game.pop_back();
		const size_t cut = game.find_last_of(L"\\/");
		game = cut == std::wstring::npos ? L"" : game.substr(0, cut + 1);
	}
	const std::wstring modToml = dir + L"d2rloader.toml";
	const std::wstring gameToml = game + L"d2rloader/config/d2rloader.toml";
	const int32_t mod = pd2::stash_toml::ReadKey(modToml, "add_shared_tabs");
	const int32_t all = mod >= 0 ? -1 : pd2::stash_toml::ReadKey(gameToml, "add_shared_tabs");
	if (mod == -2 && all == -2) {
		plog::Info("stash tabs: neither d2rloader.toml could be read: the stash page count is unknown, no tab limit");
		return UINT32_MAX;
	}
	const int32_t extra = mod >= 0 ? mod : all >= 0 ? all : 0;
	if (mod != -2 && extra < static_cast<int32_t>(aff::RequiredExtraTabs)) {
		if (RestoreExtraTabs(modToml)) {
			g_notice = "Stash tabs restored: Maps and Bossing need add_shared_tabs = 17. Restart Diablo II: Resurrected once to see them.";
			plog::Warn("stash tabs: add_shared_tabs was %d (%s): wrote add_shared_tabs = %u to the mod's d2rloader.toml (backup .bak); restart D2R once", extra,
			           mod >= 0 ? "the mod's file" : all >= 0 ? "the game-wide file" : "no live key", aff::RequiredExtraTabs);
		} else {
			g_notice = "Maps and Bossing stash tabs are hidden: set add_shared_tabs = 17 in mods/PD2Resurrected/d2rloader/config/d2rloader.toml, then restart.";
			plog::Warn("stash tabs: could not write add_shared_tabs = %u to the mod's d2rloader.toml (read-only?): set it under [d2rcore.stash] by hand and restart D2R", aff::RequiredExtraTabs);
		}
	}
	plog::Info("stash tabs: add_shared_tabs = %d from %s", extra, mod >= 0 ? "the mod's d2rloader.toml" : all >= 0 ? "the game-wide d2rloader.toml" : "the loader default (no live key)");
	return aff::MinStockSharedPages + static_cast<uint32_t>(extra);  // the fewest stock pages (Classic/LoD): below 17 a Bossing tab could sit on a missing page there
}

// The stock BankTabs to `tab`, as a click on it: the stash gets its SelectTab and shows that tab
void SelectStock(int32_t tab) noexcept {
	void* panel = reinterpret_cast<void* (*)(const char*) noexcept>(FetchPanel.address)(Panel);
	const auto find = reinterpret_cast<void* (*)(void*, const char*) noexcept>(FindChild.address);
	void* bar = panel != nullptr ? find(panel, "BankTabs") : nullptr;
	if (bar == nullptr) {
		plog::Info("stash tabs: no BankTabs to select tab %d on (panel %p)", tab, panel);
		return;
	}
	const auto select = reinterpret_cast<void (*)(void*, int32_t, bool) noexcept>(SelectTab.address);
	select(bar, tab, false);
	for (const char* ours : {"pd2rMapsTabs", "pd2rBossingTabs"}) {  // back to the hidden tab: a bar ignores a click on its active tab
		if (void* own = find(panel, ours); own != nullptr) {
			select(own, 0, false);
		}
	}
	if (void* own = g_padTab != 0 ? find(panel, "pd2rPadTabs") : nullptr; own != nullptr) {  // the pad's copy on our tab
		select(own, g_padTab - 5, false);
	}
}

void SelectShared() noexcept {
	g_opened = g_frames;
	SelectStock(1);
}

// The stash, already on its Shared tab, to shared page `page` (0-based)
void OpenPage(uint32_t page) noexcept {
	g_page = page;
	g_head = g_tail = 0;
	for (int32_t i = 0; i < LeftPresses; ++i) {
		g_queue[g_tail++] = "SharedStashLeft";
	}
	for (uint32_t i = 0; i < page; ++i) {
		g_queue[g_tail++] = "SharedStashRight";
	}
}

// The queued arrows, PerFrame at a time; false: one was not sent and stays queued for the next frame
auto Send() noexcept -> bool {
	for (uint32_t n = 0; n < PerFrame && g_head < g_tail; ++n) {
		const D2RL::Widgets::UiAction action {.structSize = D2RL::Widgets::UiActionSize,
		                                      .target = "BankPanelMessage",
		                                      .command = g_queue[g_head],
		                                      .text = ""};
		g_dispatching = true;
		const Result sent = g_widgets->dispatchUiAction(g_context, &action);
		g_dispatching = false;
		if (sent != Result::Success) {
			plog::Info("stash tabs: BankPanelMessage:%s not sent (%u), try %u", action.command, static_cast<uint32_t>(sent), g_sendFails + 1);
			if (++g_sendFails >= MaxSendFails) {  // give up: the page stays unknown (no drop filter) rather than guessed
				g_head = g_tail = g_sendFails = 0;
				g_pending = false;
				plog::Warn("stash tabs: page arrows failed %u times, the shown page is unknown", MaxSendFails);
			}
			return false;
		}
		++g_head;
		g_sendFails = 0;
	}
	return true;
}

void ShowTier(int32_t tier) noexcept {
	g_tier = tier;
	stash_affinity::ShowBase(aff::BaseCode(tier, g_base[tier]));
}

void Log(const char* what) noexcept {
	plog::Info("stash tabs: %s (view %u, tier %d, base %d)", what, static_cast<uint32_t>(g_view), g_tier + 1, g_base[g_tier]);
}

auto Open(View view) noexcept -> bool {
	const uint32_t page = view == View::Maps ? aff::MapsPage : aff::BossingPage;
	if (page >= g_pages) {  // the arrows would stop on the last page there is, under our tab
		plog::Warn("stash tabs: %s needs shared page %u but the stash has %u (add_shared_tabs below %u): tab not opened",
		           view == View::Maps ? "Maps" : "Bossing", page + 1, g_pages, aff::RequiredExtraTabs);
		return false;
	}
	g_view = view;
	OpenPage(page);
	if (view == View::Maps) {
		ShowTier(g_tier);
	}
	g_pending = true;  // the page is known once every arrow is sent (OnFrame)
	stash_affinity::SetTabOpen(true, 0);
	Log(view == View::Maps ? "Maps" : "Bossing");
	return true;
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

// The pad's steps on the Maps tab: the same ShowTier the buttons' clicks call
void StepTier() noexcept {
	ShowTier((g_tier + 1) % 3);
	Log("pad tier");
}

void StepBase(int32_t by) noexcept {
	int32_t i = g_base[g_tier];
	do {
		i = (i + by + aff::PageWidth) % aff::PageWidth;
	} while (aff::BaseCode(g_tier, i) == 0);  // the tier has at least the base on show
	g_base[g_tier] = i;
	ShowTier(g_tier);
	Log("pad base");
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
	g_bossingOn {"pd2rBossingTabsOn"}, g_bossingOnText {"pd2rBossingTabsOnText"}, g_padTabs {"pd2rPadTabs"}, g_mapsBar {"pd2rMapsTabs"}, g_bossingBar {"pd2rBossingTabs"}, g_mapsLegend {"pd2rMapsLegend"};
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
	g_padTab = 0;
	OpenPage(0);  // the stash is still on Shared under ours: page 1 now, so a stock Shared tab never opens on a reserved page
	g_pending = false;
	while (g_head < g_tail && Send()) {
	}
	stash_affinity::SetTabOpen(false);
	Show(g_navigation, true);
	ShowButton(g_cube, false);
}

// The pad on our tab (5 Maps, 6 Bossing) as a mouse click on it opens it, the stock bar going to Shared next frame
void OpenPad(View view) noexcept {
	g_padTab = view == View::Maps ? 5 : 6;
	if (view != g_view && !Open(view)) {
		g_padTab = 0;
		return;
	}
	g_opened = g_frames;
	g_wantShared = g_select;
}

// The pad's LT/RT along Personal..Runes, Maps, Bossing (round from either end); true: done here, the press is consumed
auto PadTab(bool right) noexcept -> bool {
	if (!g_select) {
		return false;  // no Shared select: our tabs are the mouse's only
	}
	if (g_view == View::None) {
		if (g_tab != (right ? 4 : 0)) {
			return false;  // the stock bar's own step
		}
		OpenPad(right ? View::Maps : View::Bossing);
	} else if (g_view == View::Maps && !right) {
		Leave();
		Log("Runes");
		SelectStock(4);
	} else if (g_view == View::Bossing && right) {
		Leave();
		Log("Personal");
		SelectStock(0);
	} else {
		OpenPad(right ? View::Bossing : View::Maps);
	}
	return true;
}

// The pad's actions reach the stash as InputMessage/ControllerActionBegin in its OnMessage, whatever the binding, pad
// index or pad type (XInput, DS4, Steam Input): field 0x22 points to an action struct whose id at +0x10 is the bound
// action. No SDK event carries it, so, as altar_panel.cpp does for B, the stash panel object alone gets a copy of its
// vtable with OnMessage (slot 4) wrapped; no game code is patched and every other panel keeps D2R's.
constexpr uint64_t InputMessageHash = 0x4ae067c6248b6042;           // "InputMessage", the message's first field
constexpr uint64_t ControllerActionBeginHash = 0x1e439768f7dd4594;  // "ControllerActionBegin", field 0x11
constexpr uint64_t ControllerActionEndHash = 0x4d833cc71f08b3c0;    // "ControllerActionEnd"
enum PadAction : int { TabLeft = 7, TabRight = 8, CycleLeft = 0x13, CycleRight = 0x14, Option3 = 3 };
constexpr size_t VtableMax = 256;
void* g_vtable[VtableMax + 1] = {};  // [0] the RTTI slot before the table
void** g_panelVtable = nullptr;      // the panel's own vtable, copied into g_vtable
using OnMessageFn = void (*)(void*, const uint64_t*) noexcept;
OnMessageFn g_onMessage = nullptr;

// UI thread: cycleLeft (LB) is held. D2R sends the LB+LT/RT chord as plain tabLeft/tabRight (the modifier is in the pad
// state, not the message), so the held cycleLeft is what tells a Shared page step from a tab step.
bool g_cycleHeld = false;

// One switch on the action id; true: done here, the press is consumed. A new pad action of the stash is a new case.
auto OnPadAction(int32_t id) noexcept -> bool {
	switch (id) {
	case TabLeft:
	case TabRight:
		if (g_cycleHeld) {  // LB + LT/RT: a Shared page step
			return g_view != View::None || (g_tab == 1 && !PlayerPage(id == TabRight, "pad page right"));
		}
		return PadTab(id == TabRight);
	case CycleLeft:
	case CycleRight:
		if (g_view == View::Maps) {
			StepBase(id == CycleRight ? 1 : -1);  // LB / RB: the tier's previous / next base, as a click on its button
		}
		if (g_view != View::None) {
			return true;  // our tab's page stays ours (the mouse's arrows are hidden there)
		}
		return g_tab == 1 && !PlayerPage(id == CycleRight, "pad page right");
	case Option3:  // the left stick's click: nothing in the stash uses it (the right stick's is Hold to Sort, option4)
		if (g_view != View::Maps) {
			return false;
		}
		StepTier();  // the next tier, as a click on its button
		return true;
	default:
		return false;
	}
}

void StashOnMessage(void* panel, const uint64_t* message) noexcept {
	if (message != nullptr && message[0] == InputMessageHash && message[0x11] == ControllerActionEndHash) {
		const auto* action = reinterpret_cast<const uint8_t*>(message[0x22]);
		if (action != nullptr && *reinterpret_cast<const int32_t*>(action + 0x10) == CycleLeft) {
			g_cycleHeld = false;
		}
	} else if (message != nullptr && message[0] == InputMessageHash && message[0x11] == ControllerActionBeginHash) {
		const auto* action = reinterpret_cast<const uint8_t*>(message[0x22]);
		if (action != nullptr && action[0x18] == 0 && !reinterpret_cast<bool (*)() noexcept>(MessageHandled.address)()) {
			const int32_t id = *reinterpret_cast<const int32_t*>(action + 0x10);
			if (id == TabLeft || id == TabRight || id == CycleLeft || id == CycleRight || id == Option3) {
				plog::Info("stash tabs: pad action %d (view %u, tab %d)", id, static_cast<uint32_t>(g_view), g_tab);
			}
			g_cycleHeld = id == CycleLeft ? true : g_cycleHeld;
			if (g_stashOpen && OnPadAction(id)) {
				reinterpret_cast<void (*)() noexcept>(MarkHandled.address)();
				return;
			}
		}
	}
	g_onMessage(panel, message);
}

auto Executable(void* p) noexcept -> bool {
	MEMORY_BASIC_INFORMATION info {};
	return p != nullptr && VirtualQuery(p, &info, sizeof info) == sizeof info && info.State == MEM_COMMIT &&
	       (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// UI thread: gives the stash panel object the vtable copy (once per panel object)
void WrapOnMessage() noexcept {
	void* panel = reinterpret_cast<void* (*)(const char*) noexcept>(FetchPanel.address)(Panel);
	if (panel == nullptr) {
		return;
	}
	void**& vtable = *static_cast<void***>(panel);
	if (vtable == g_vtable + 1) {
		return;
	}
	if (g_panelVtable != nullptr && g_panelVtable != vtable) {
		static bool logged = false;
		if (!std::exchange(logged, true)) {
			plog::Info("stash tabs: panel vtable changed, pad tabs not handled");
		}
		return;
	}
	size_t n = 0;
	while (n < VtableMax && Executable(vtable[n])) {
		++n;
	}
	if (n <= 4) {
		plog::Info("stash tabs: panel vtable has %zu methods, pad tabs not handled", n);
		return;
	}
	if (g_panelVtable == nullptr) {
		plog::Info("stash tabs: panel vtable[4] %p wrapped (%zu methods)", vtable[4], n);
	}
	g_panelVtable = vtable;
	g_vtable[0] = vtable[-1];
	std::memcpy(g_vtable + 1, vtable, n * sizeof(void*));
	g_onMessage = reinterpret_cast<OnMessageFn>(vtable[4]);
	g_vtable[1 + 4] = reinterpret_cast<void*>(&StashOnMessage);
	vtable = g_vtable + 1;
}

auto OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
	-> D2RL::SharedEvents::UiMessageAction {
	using Action = D2RL::SharedEvents::UiMessageAction;
	if (event == nullptr || event->target == nullptr || event->command == nullptr) {
		return Action::Continue;
	}
	const char* text = event->text != nullptr ? event->text : "";
	if (std::strcmp(event->target, "BankPanelMessage") == 0) {
		const bool right = std::strcmp(event->command, "SharedStashRight") == 0;
		if (!g_dispatching && g_view == View::None && (right || std::strcmp(event->command, "SharedStashLeft") == 0)) {
			g_tab = 1;  // the arrows are the Shared tab's
			return PlayerPage(right, "right arrow") ? Action::Continue : Action::Consume;
		}
		if (g_dispatching || std::strcmp(event->command, "SelectTab") != 0) {
			return Action::Continue;
		}
		g_tab = -1;  // the pad's bar names the tab; the mouse's names none
		for (int32_t i = 0; i < 5; ++i) {
			g_tab = std::strcmp(text, TabNames[i]) == 0 ? i : g_tab;
		}
		if (g_view != View::None && g_frames - g_opened > OursFrames) {  // not our tab's own SelectTab, nor our select's
			Leave();
			Log("stock tab");
		}
		return Action::Continue;  // the stash selects the tab (ours: Shared)
	}
	if (std::strcmp(event->target, "PanelManager") != 0) {
		return Action::Continue;
	}
	// Every close sends ClosePanel:BankExpansionLayout (seen for Esc, its X, I, pad B and pad Start). getWidgetState
	// would read the panel's visibility, but D2RLoader 1.3.1's widget service ends before that field.
	if (std::strcmp(text, Panel) == 0) {
		g_stashOpen = std::strcmp(event->command, "OpenPanel") == 0 || (g_stashOpen && std::strcmp(event->command, "ClosePanel") != 0);
		g_cycleHeld = g_stashOpen && g_cycleHeld;  // a close with LB down sends no release
		return Action::Continue;
	}
	if (std::strcmp(event->command, "OpenPanel") != 0 || std::strncmp(text, Prefix, sizeof Prefix - 1) != 0) {
		return Action::Continue;
	}
	const char* what = text + sizeof Prefix - 1;
	if (std::strcmp(what, "TabMaps") == 0 || std::strcmp(what, "TabBossing") == 0) {
		if (Open(what[3] == 'M' ? View::Maps : View::Bossing)) {
			g_opened = g_frames;
			g_wantShared = g_select;
		}
	} else if (std::strcmp(what, "PadMaps") == 0 || std::strcmp(what, "PadBossing") == 0) {
		// the pad's bar on our tab, a press PadTab did not catch: the next frame puts it back on Shared
		OpenPad(what[3] == 'M' ? View::Maps : View::Bossing);
	} else if (std::strcmp(what, "Shared") == 0) {  // the stock Shared tab, already selected under ours: its first page
		Leave();
		g_tab = 1;
		Log("Shared");
	} else if (std::strcmp(what, "Hidden") != 0) {  // Hidden: our bars' off-screen tab 0 (an empty message asserts, #1649)
		Click(what);
	}
	return Action::Consume;
}

// Console `core-ui stash <tab>` (#1740): the request is read on the UI thread, as the console may not run there.
// g_request: 0 none, 1-4 Personal/Materials/Maps/Bossing, 100 + n shared page n (1-based)
std::atomic<int32_t> g_request {0};
int32_t g_act = 0, g_actFrame = 0;  // UI thread: the request waiting for the opened stash, and the frame to act on

auto DispatchPanel(const char* command, const char* text) noexcept -> Result {
	const D2RL::Widgets::UiAction action {.structSize = D2RL::Widgets::UiActionSize, .target = "PanelManager", .command = command, .text = text};
	return g_widgets->dispatchUiAction(g_context, &action);
}

void RunRequest() noexcept {
	if (g_act == 0) {
		g_act = g_request.exchange(0);
		if (g_act == 0) {
			return;
		}
		if (!g_stashOpen) {
			if (const Result r = DispatchPanel("OpenPanel", Panel); r != Result::Success) {
				plog::Warn("stash tabs: console open failed (%u)", static_cast<uint32_t>(r));
				g_act = 0;
				return;
			}
		}
		g_actFrame = static_cast<int32_t>(g_frames) + 5;  // the panel needs a few frames to show
		return;
	}
	if (static_cast<int32_t>(g_frames) < g_actFrame) {
		return;
	}
	const int32_t what = std::exchange(g_act, 0);
	if (what == 3 || what == 4) {
		if (const Result r = DispatchPanel("OpenPanel", what == 3 ? "core-ui/StashTabMaps" : "core-ui/StashTabBossing"); r != Result::Success) {
			plog::Warn("stash tabs: console open failed (%u)", static_cast<uint32_t>(r));
			return;
		}
	} else if (what == 1 || what == 2) {
		Leave();
		g_tab = what == 1 ? 0 : 3;
		SelectStock(g_tab);
		g_opened = g_frames;
	} else {
		Leave();
		g_tab = 1;
		SelectShared();
		OpenPage(static_cast<uint32_t>(what - 101));
	}
	plog::Info("stash tabs: console opened %s (page %u)", what == 1 ? "Personal" : what == 2 ? "Materials" : what == 3 ? "Maps" : what == 4 ? "Bossing" : "Shared", g_page + 1);
}

// UI thread
void OnFrame(const D2RL::PluginContext*, const D2RL::Overlay::Frame* frame, void*) noexcept {
	++g_frames;
	if (g_notice != nullptr && g_inGame.load() && g_noticeLeft > 0.0f) {
		g_noticeLeft -= frame->deltaTimeSeconds;
		const D2RL::Overlay::TextRequest text {.structSize = D2RL::Overlay::TextRequestSize,
		                                       .canvas = frame->canvas,
		                                       .position = {frame->screenWidth * 0.02f, frame->screenHeight * 0.05f},
		                                       .color = {1.0f, 0.35f, 0.3f, 1.0f},
		                                       .text = g_notice,
		                                       .textLength = static_cast<uint32_t>(std::strlen(g_notice))};
		const D2RL::Overlay::Result drawn = g_overlay->drawText(g_context, &text);
		if (!std::exchange(g_noticeLogged, true)) {
			plog::Info("stash tabs: the on-screen notice is up (draw result %u)", static_cast<uint32_t>(drawn));
		}
	}
	if (g_reset.exchange(false)) {
		g_view = View::None;
		g_head = g_tail = g_page = g_sendFails = 0;
		g_pending = false;
		g_tab = g_padTab = 0;  // a new game opens the stash on Personal
		g_stashOpen = false;
		g_cycleHeld = false;
		stash_affinity::ShowBase(0);
		stash_affinity::SetTabOpen(false);
		g_request = 0;  // a command typed before the game ended must not fire in the next one
		g_act = 0;
	}
	if (g_panel == D2RL::Widgets::InvalidHandle && g_widgets->findPanel(g_context, Panel, &g_panel) != Result::Success) {
		g_panel = D2RL::Widgets::InvalidHandle;
		return;
	}
	RunRequest();
	if (g_select && g_stashOpen) {
		WrapOnMessage();
	}
	if (g_wantShared) {
		g_wantShared = false;
		SelectShared();
	}
	if (g_frames - g_opened > OursFrames) {
		Send();  // the page arrows, once the stash is on its Shared tab (it reads them there only)
		if (g_pending && g_view != View::None && g_head == g_tail) {  // every arrow sent: the page is the one asked for
			g_pending = false;
			stash_affinity::SetTabOpen(true, g_page);
			plog::Info("stash tabs: shared page %u on show (all %u arrows sent)", g_page + 1, LeftPresses + g_page);
		}
	}
	if (g_pages <= aff::BossingPage) {  // no such page: its tab is gone, not a button onto another page
		ShowButton(g_bossingBar, false);
		if (g_pages <= aff::MapsPage) {
			ShowButton(g_mapsBar, false);
		}
	}
	const bool ours = g_view != View::None, maps = g_view == View::Maps;
	Show(g_mapsOn, maps);
	Show(g_mapsOnText, maps);
	Show(g_bossingOn, g_view == View::Bossing);
	Show(g_bossingOnText, g_view == View::Bossing);
	Show(g_background, maps);
	Show(g_mapsLegend, maps);
	Show(g_bossingBackground, g_view == View::Bossing);
	ShowButton(g_sharedOff, ours);  // the stock Shared tab, lit under ours, drawn unlit: one tab looks selected
	ShowButton(g_padTabs, ours);   // the pad layout's Maps and Bossing tabs, ours lit
	if (ours) {
		Show(g_navigation, false);  // the page stays ours
		ShowButton(g_cube, true);  // enabled too: off a RotW tab the stash disables it, and its grid and Transmute with it
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

void OnGameLeft(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	g_inGame.store(false);  // the notice is not drawn on the menu
}

void OnPlayerReady(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	g_inGame.store(true);
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
	g_pages = ReadPages(context->pluginConfigPath);
	if (g_pages <= aff::BossingPage) {
		plog::Warn("stash tabs: the stash has %u shared pages (add_shared_tabs below %u): the %s hidden", g_pages, aff::RequiredExtraTabs,
		           g_pages <= aff::MapsPage ? "Maps and Bossing tabs are" : "Bossing tab is");
	}
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
	const D2RL::Lifecycle::GameplayEventListener left {.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
	                                                  .kind = D2RL::Lifecycle::GameplayEventKind::GameLeft,
	                                                  .callback = OnGameLeft};
	D2RL::Lifecycle::ListenerHandle leftHandle = D2RL::Lifecycle::InvalidHandle;
	const D2RL::SharedEvents::UiMessageListener listener {.structSize = D2RL::SharedEvents::UiMessageListenerSize,
	                                                      .callback = OnUiMessage};
	const D2RL::Overlay::CallbackRegistration frame {.structSize = D2RL::Overlay::CallbackRegistrationSize,
	                                                 .phase = D2RL::Overlay::Phase::AfterGameUi,
	                                                 .callback = OnFrame};
	D2RL::Lifecycle::ListenerHandle readyHandle = D2RL::Lifecycle::InvalidHandle;
	D2RL::SharedEvents::ListenerHandle listenerHandle = D2RL::SharedEvents::InvalidHandle;
	D2RL::Overlay::CallbackHandle frameHandle = D2RL::Overlay::InvalidCallbackHandle;
	if (lifecycle->registerGameplayEventListener(context, &ready, &readyHandle) != D2RL::Lifecycle::Result::Success ||
	    lifecycle->registerGameplayEventListener(context, &left, &leftHandle) != D2RL::Lifecycle::Result::Success ||
	    events->registerUiMessageListener(context, &listener, &listenerHandle) != D2RL::SharedEvents::Result::Success ||
	    g_overlay->registerFrameCallback(context, &frame, &frameHandle) != D2RL::Overlay::Result::Success) {
		std::snprintf(report, size, "FAILED (a listener was refused)");
		return;
	}
	std::snprintf(report, size, "Maps and Bossing stash tabs on a row above the stock tabs; Shared tab select %s",
	              g_select ? "by signature" : "off (signature not found: the tabs work from the Shared tab only)");
}

auto Console(const char* args, char* reply, size_t size) noexcept -> bool {
	int32_t what = 0;
	const char* const words[] = {"personal", "materials", "maps", "bossing"};
	for (int32_t i = 0; i < 4; ++i) {
		what = std::strcmp(args, words[i]) == 0 ? i + 1 : what;
	}
	char* end = nullptr;
	const long page = what == 0 ? std::strtol(args, &end, 10) : 0;
	if (what == 0 && page >= 1 && page <= static_cast<long>(aff::LastReservedPage + 1) && end != args && *end == '\0') {
		what = 100 + static_cast<int32_t>(page);
	}
	if (what == 0 || g_widgets == nullptr || !g_select) {
		std::snprintf(reply, size, "usage: core-ui stash personal|materials|maps|bossing|<shared page 1-%u>%s", aff::LastReservedPage + 1,
		              g_select ? "" : " (off: stash tab signatures not found)");
		return false;
	}
	g_request = what;
	std::snprintf(reply, size, "core-ui stash: opening %s, see the log line 'stash tabs: console opened'", args);
	return true;
}

void Counters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "stash tabs: view %u, tier %d, %u clicks", static_cast<uint32_t>(g_view), g_tier + 1,
	              g_clicks.load());
}

}  // namespace stash_tabs
