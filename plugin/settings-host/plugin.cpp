// aa-settings-host: the settings host Pyre-Flies' companions look for (provider plugin id "aa-settings-host", service
// "settings" v1, event "ready" v1). The id is theirs and must match. Load order does not matter:
// a client that loads first waits for "ready". No public plugin provides it, so without this one their Options rows never show (#2237).
// Client-only, no hook; it keeps the ADR 0004 image gate.
//
// D2RLoader 1.3.1's WidgetService ends at getInputText: no widget text can change and no dropdown or toggle can be
// read or set. So the rows are static layout rows (gameplayoptionspanelhd.json, names from rows.h). A toggle row is a
// checkbox button whose ticked art this plugin shows and hides. A dropdown row is D2R's own DropdownListWidget with
// static states (rows.h); no SDK call reads or sets its pick, so this plugin calls D2R's panel fetch, child find and
// dropdown select, found by signature, and reads the picked state from the widget. A missing signature leaves the
// dropdowns off. Sliders and status rows need runtime text: refused (Unavailable), they stay toml-only.
// A click applies at once (the Options panel's own Apply button does not cover plugin rows).
//
// Pyre's rows sit in Esc > Loot Filter (LootFilterOptionsPanel, #2240), under "PyreRows". This plugin hides them while
// Pyre has no rows; while it has, it shows them, hides D2R's LootFilterProfilePanel (its .fltr picker and editor) and clears D2R's active filter
// name, which D2R sets from lootfilter.json when a character joins: the native filter goes by that name only, and
// Pyre stops while it is set. lootfilter.json is not touched, so without Pyre D2R's own pick comes back.
#include <D2RLPlugin/api.h>

#include "crash_guard.h"
#include "log.h"
#include "off_switch.h"
#include "parts.h"
#include "pd2_engine.h"
#include "rows.h"
#include "sig.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifndef MODKIT_PLUGIN_NAME
#define MODKIT_PLUGIN_NAME "aa-settings-host"
#endif
#ifndef MODKIT_PLUGIN_VERSION
#define MODKIT_PLUGIN_VERSION "0.2.7"
#endif

namespace {

// The service's ABI as its clients call it (plain C structs, fields appended at the end).
namespace abi {
enum class Result : uint32_t { Success = 0, InvalidArgument = 1, Conflict = 2, Unavailable = 3 };
using RowHandle = uint64_t;
using ToggleAppliedFn = void(__cdecl*)(bool value, void* userData) noexcept;
using IntAppliedFn = void(__cdecl*)(int32_t value, void* userData) noexcept;
using RefreshToggleFn = bool(__cdecl*)(void* userData) noexcept;
using RefreshIntFn = int32_t(__cdecl*)(void* userData) noexcept;
inline constexpr uint32_t MaxDropdownOptions = 250;

struct ToggleSpec {
	uint32_t structSize;
	uint32_t section;
	const char* pluginId;
	const char* key;
	const char* label;
	const char* tooltip;
	bool value;
	bool defaultValue;
	ToggleAppliedFn onApplied;
	void* userData;
	RefreshToggleFn onRefresh;
};
struct DropdownSpec {
	uint32_t structSize;
	uint32_t section;
	const char* pluginId;
	const char* key;
	const char* label;
	const char* tooltip;
	const char* const* options;
	uint32_t optionCount;
	int32_t selectedIndex;
	int32_t defaultIndex;
	IntAppliedFn onApplied;
	void* userData;
	RefreshIntFn onRefresh;
};
struct ServiceV1 {
	uint32_t serviceSize;
	uint32_t serviceVersion;
	Result(__cdecl* addToggle)(const ToggleSpec*, RowHandle*) noexcept;
	void(__cdecl* removeRow)(RowHandle) noexcept;
	Result(__cdecl* addSlider)(const void*, RowHandle*) noexcept;
	Result(__cdecl* setToggle)(RowHandle, bool) noexcept;
	Result(__cdecl* setSlider)(RowHandle, int32_t) noexcept;
	Result(__cdecl* addDropdown)(const DropdownSpec*, RowHandle*) noexcept;
	Result(__cdecl* setDropdown)(RowHandle, int32_t) noexcept;
	Result(__cdecl* setDropdownOptions)(RowHandle, const char* const*, uint32_t, int32_t) noexcept;
	Result(__cdecl* addStatus)(const void*, RowHandle*) noexcept;
	Result(__cdecl* setStatus)(RowHandle, const char*) noexcept;
};
static_assert(sizeof(ToggleSpec) == 72 && sizeof(DropdownSpec) == 88 && sizeof(ServiceV1) == 88);
}  // namespace abi

constexpr const char* PluginName = MODKIT_PLUGIN_NAME;
constexpr const char* PluginVersion = MODKIT_PLUGIN_VERSION;
constexpr const char* Panels[] = {settings_rows::OptionsPanel, settings_rows::LootFilterPanel};
constexpr uint32_t WatchFrames = 6;  // the padlock's pace: a few frames after a click or an open is soon enough

// D2R's calls for the dropdowns (93847 RVAs for reference only), as core-ui's stash tabs
// 93847: 0x846170, void* (name): PanelManager::FetchPanel
sigscan::Entry FetchPanel {"PanelFetch", "settings", sigscan::Kind::Code, 0, 0, "48 8b d1 48 8b 0d ?? ?? ?? ?? 48 85 c9 0f 85 ?? ?? ?? ??"};
// 93847: 0x855c90, void* (widget, name): the named child widget, searched down the tree
sigscan::Entry FindChild {"WidgetFind", "settings", sigscan::Kind::Code, 0, 0,
                          "40 53 55 56 57 41 54 48 83 ec 20 4c 89 7c 24 68 48 8b f2 48 8b f9 e8 ?? ?? ?? ?? 44 0f b6 80 18 01 00 00"};
// 93847: 0x8efbb0, void (dropdown, index): DropdownListWidget's select (shows states[index], sends no message); the
// pattern holds the count field's offset (0x3794), so a build that moves the fields misses here
sigscan::Entry DropdownSelect {"DropdownSelect", "settings", sigscan::Kind::Code, 0, 0,
                               "40 56 57 48 81 ec 88 00 00 00 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 44 24 60 48 63 f2 48 8b f9 3b b1 94 37 00 00 7c"};
sigscan::Entry* const Sigs[] = {&FetchPanel, &FindChild, &DropdownSelect};
constexpr size_t DropdownCount = 0x3794;     // int32 state count
constexpr size_t DropdownSelected = 0x3798;  // int32 picked state, written before its onUpdateMessage goes out
bool g_native = false;
// D2R's loot filter config (LootFilterConfig, its active profile name a string {data, size, capacity} at +0x20) and
// its clear. No service covers D2R's loot filter. Both from the character join (93847: 0x9b360), which reads
// lootfilter.json and selects or clears the profile.
// 93847: 0x1bf9d0, LootFilterConfig* (): the config singleton
sigscan::Entry LootConfig {"LootFilterConfig", "settings", sigscan::Kind::Rip, 12, 16,
                           "48 8b cf e8 ?? ?? ?? ?? 48 8b f8 e8 ?? ?? ?? ?? 80 3f 00 4c 8b f8 74"};
// 93847: 0x1bf520, void (config): empties the active profile name
sigscan::Entry LootClear {"LootFilterClear", "settings", sigscan::Kind::Rip, 21, 25,
                          "80 7d 37 00 49 8b cf 74 0b 48 8d 55 0f e8 ?? ?? ?? ?? eb 05 e8 ?? ?? ?? ?? 80 7d 37 00"};
sigscan::Entry* const YieldSigs[] = {&LootConfig, &LootClear};
constexpr size_t ActiveNameSize = 0x28;
bool g_yield = false;  // both found: Pyre's rows replace D2R's filter screen

struct Row {
	std::string name;  // settings_rows::RowName
	bool dropdown = false;
	bool removed = false;
	bool value = false;  // toggle
	int32_t index = 0;   // dropdown
	int32_t shown = -1;  // the state its widget showed at the last sync or pick (OnPick tells the picked dropdown by it)
	std::vector<std::string> options;
	abi::ToggleAppliedFn onToggle = nullptr;
	abi::IntAppliedFn onIndex = nullptr;
	abi::RefreshToggleFn refreshToggle = nullptr;
	abi::RefreshIntFn refreshIndex = nullptr;
	void* userData = nullptr;
};

const D2RL::PluginContext* g_context = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;
std::atomic<const D2RL::PluginCommunicationService*> g_comms {nullptr};  // until "ready" is published
std::mutex g_lock;
std::deque<Row> g_rows;  // handle = index + 1; a deque keeps the rows in place as more are added
std::atomic<uint32_t> g_frames {0}, g_clicks {0};
std::atomic<bool> g_dirty[std::size(Panels)] {true, true};
std::atomic<int> g_pyreRows {0};    // Pyre's live rows: while any, Pyre owns Esc > Loot Filter
std::atomic<bool> g_soon {false};   // the loot filter screen opens: sync on the next frame
bool g_panelSeen[std::size(Panels)] {};  // UI thread only

void Dirty() noexcept {
	for (auto& dirty : g_dirty) {
		dirty = true;
	}
}

auto IsPyre(const Row& row) -> bool {
	return settings_rows::PanelOf(row.name) == settings_rows::LootFilterPanel;
}

auto Find(abi::RowHandle handle) -> Row* {
	return handle == 0 || handle > g_rows.size() || g_rows[handle - 1].removed ? nullptr : &g_rows[handle - 1];
}

auto Add(Row&& row, abi::RowHandle* handle) -> abi::Result {
	const std::lock_guard guard(g_lock);
	for (const Row& old : g_rows) {
		if (!old.removed && old.name == row.name) {
			return abi::Result::Conflict;
		}
	}
	g_pyreRows += IsPyre(row) ? 1 : 0;
	g_rows.push_back(std::move(row));
	*handle = g_rows.size();
	Dirty();
	return abi::Result::Success;
}

auto __cdecl AddToggle(const abi::ToggleSpec* spec, abi::RowHandle* handle) noexcept -> abi::Result {
	if (spec == nullptr || handle == nullptr || spec->structSize < offsetof(abi::ToggleSpec, onRefresh) ||
	    spec->pluginId == nullptr || spec->key == nullptr) {
		return abi::Result::InvalidArgument;
	}
	try {
		Row row;
		row.name = settings_rows::RowName(spec->pluginId, spec->key);
		row.value = spec->value;
		row.onToggle = spec->onApplied;
		row.userData = spec->userData;
		row.refreshToggle = spec->structSize >= sizeof(abi::ToggleSpec) ? spec->onRefresh : nullptr;
		return Add(std::move(row), handle);
	} catch (...) {
		return abi::Result::Unavailable;
	}
}

auto CopyOptions(const char* const* options, uint32_t count, std::vector<std::string>& out) -> bool {
	if (options == nullptr || count == 0 || count > abi::MaxDropdownOptions) {
		return false;
	}
	out.clear();
	for (uint32_t i = 0; i < count; ++i) {
		if (options[i] == nullptr) {
			return false;
		}
		out.emplace_back(options[i]);
	}
	return true;
}

auto __cdecl AddDropdown(const abi::DropdownSpec* spec, abi::RowHandle* handle) noexcept -> abi::Result {
	if (spec == nullptr || handle == nullptr || spec->structSize < offsetof(abi::DropdownSpec, onRefresh) ||
	    spec->pluginId == nullptr || spec->key == nullptr) {
		return abi::Result::InvalidArgument;
	}
	try {
		Row row;
		if (!CopyOptions(spec->options, spec->optionCount, row.options)) {
			return abi::Result::InvalidArgument;
		}
		row.name = settings_rows::RowName(spec->pluginId, spec->key);
		row.dropdown = true;
		row.index = spec->selectedIndex;
		row.onIndex = spec->onApplied;
		row.userData = spec->userData;
		row.refreshIndex = spec->structSize >= sizeof(abi::DropdownSpec) ? spec->onRefresh : nullptr;
		return Add(std::move(row), handle);
	} catch (...) {
		return abi::Result::Unavailable;
	}
}

void __cdecl RemoveRow(abi::RowHandle handle) noexcept {
	const std::lock_guard guard(g_lock);
	if (Row* row = Find(handle)) {
		row->removed = true;
		g_pyreRows -= IsPyre(*row) ? 1 : 0;
		Dirty();
	}
}

auto __cdecl SetToggle(abi::RowHandle handle, bool value) noexcept -> abi::Result {
	const std::lock_guard guard(g_lock);
	Row* row = Find(handle);
	if (row == nullptr || row->dropdown) {
		return abi::Result::InvalidArgument;
	}
	row->value = value;
	Dirty();
	return abi::Result::Success;
}

auto __cdecl SetDropdown(abi::RowHandle handle, int32_t index) noexcept -> abi::Result {
	const std::lock_guard guard(g_lock);
	Row* row = Find(handle);
	if (row == nullptr || !row->dropdown) {
		return abi::Result::InvalidArgument;
	}
	row->index = index;
	Dirty();
	return abi::Result::Success;
}

auto __cdecl SetDropdownOptions(abi::RowHandle handle, const char* const* options, uint32_t count, int32_t index) noexcept
	-> abi::Result {
	const std::lock_guard guard(g_lock);
	Row* row = Find(handle);
	if (row == nullptr || !row->dropdown) {
		return abi::Result::InvalidArgument;
	}
	try {
		std::vector<std::string> copy;
		if (!CopyOptions(options, count, copy)) {
			return abi::Result::InvalidArgument;
		}
		row->options = std::move(copy);
		row->index = index;
		Dirty();
		return abi::Result::Success;
	} catch (...) {
		return abi::Result::Unavailable;
	}
}

// Sliders and status rows show runtime text, which no 1.3.1 widget call can write.
auto __cdecl Refuse(const void*, abi::RowHandle*) noexcept -> abi::Result {
	return abi::Result::Unavailable;
}
auto __cdecl SetSlider(abi::RowHandle, int32_t) noexcept -> abi::Result {
	return abi::Result::InvalidArgument;
}
auto __cdecl SetStatus(abi::RowHandle, const char*) noexcept -> abi::Result {
	return abi::Result::InvalidArgument;
}

constexpr abi::ServiceV1 Service {
	.serviceSize = sizeof(abi::ServiceV1),
	.serviceVersion = 1,
	.addToggle = AddToggle,
	.removeRow = RemoveRow,
	.addSlider = Refuse,
	.setToggle = SetToggle,
	.setSlider = SetSlider,
	.addDropdown = AddDropdown,
	.setDropdown = SetDropdown,
	.setDropdownOptions = SetDropdownOptions,
	.addStatus = Refuse,
	.setStatus = SetStatus,
};

// The row's DropdownListWidget, or nullptr (signatures missing, panel gone, or no dropdown of the table's size)
auto NativeDropdown(const settings_rows::Dropdown& d) noexcept -> uint8_t* {
	if (!g_native) {
		return nullptr;
	}
	void* panel = reinterpret_cast<void* (*)(const char*) noexcept>(FetchPanel.address)(settings_rows::PanelOf(d.row));
	const auto find = reinterpret_cast<void* (*)(void*, const char*) noexcept>(FindChild.address);
	auto* widget = panel != nullptr ? static_cast<uint8_t*>(find(panel, d.widget)) : nullptr;
	return widget != nullptr && *reinterpret_cast<int32_t*>(widget + DropdownCount) == static_cast<int32_t>(d.count) ? widget : nullptr;
}

void Show(D2RL::Widgets::WidgetHandle panel, const std::string& name, bool shown) noexcept {
	D2RL::Widgets::WidgetHandle widget {};
	if (g_widgets->findWidget(g_context, panel, name.c_str(), &widget) == D2RL::Widgets::Result::Success) {
		g_widgets->setWidgetVisible(g_context, widget, shown);
		g_widgets->setWidgetEnabled(g_context, widget, shown);  // a hidden button would still take the hover
	}
}

// The owners' refresh callbacks may call back into the service, so they run without the lock.
void RefreshAll() noexcept {
	struct Pull {
		abi::RowHandle handle;
		abi::RefreshToggleFn toggle;
		abi::RefreshIntFn index;
		void* userData;
	};
	std::vector<Pull> pulls;
	try {
		const std::lock_guard guard(g_lock);
		for (size_t i = 0; i < g_rows.size(); ++i) {
			const Row& row = g_rows[i];
			if (!row.removed && (row.refreshToggle != nullptr || row.refreshIndex != nullptr)) {
				pulls.push_back({i + 1, row.refreshToggle, row.refreshIndex, row.userData});
			}
		}
	} catch (...) {
		return;
	}
	for (const Pull& pull : pulls) {
		if (pull.toggle != nullptr) {
			(void)SetToggle(pull.handle, pull.toggle(pull.userData));
		} else {
			(void)SetDropdown(pull.handle, pull.index(pull.userData));
		}
	}
}

// UI thread: each registered row shows the button that matches its value
void OnFrame(const D2RL::PluginContext*, const D2RL::Overlay::Frame*, void*) noexcept {
	// Clients that loaded first (the loader sorts file names case-sensitively: "Item Roll Ranges.dll" comes before
	// us) subscribed to "ready" and acquire the service when it fires. An acquire fails while our load call runs, so
	// it fires on the first frame.
	if (const D2RL::PluginCommunicationService* comms = g_comms.exchange(nullptr)) {
		const D2RL::PluginCommunication::PublishEventRequest ready {
			.structSize = D2RL::PluginCommunication::PublishEventRequestSize,
			.name = "ready",
			.eventVersion = 1,
		};
		const auto result = comms->publishEvent(g_context, &ready);
		plog::Info("published \"ready\" for the companions that loaded first (result %u)", static_cast<unsigned>(result));
	}
	// Every frame, so the native filter set on a character's join never gets to run.
	const bool pyre = g_yield && g_pyreRows > 0;
	if (pyre) {
		auto* config = reinterpret_cast<uint8_t* (*)() noexcept>(LootConfig.address)();
		if (config != nullptr && *reinterpret_cast<size_t*>(config + ActiveNameSize) != 0) {
			reinterpret_cast<void (*)(void*) noexcept>(LootClear.address)(config);
		}
	}
	if (++g_frames % WatchFrames != 0 && !g_soon.exchange(false)) {
		return;
	}
	for (size_t p = 0; p < std::size(Panels); ++p) {
		D2RL::Widgets::WidgetHandle panel {};
		if (g_widgets->findPanel(g_context, Panels[p], &panel) != D2RL::Widgets::Result::Success) {
			g_panelSeen[p] = false;
			continue;
		}
		if (Panels[p] == settings_rows::LootFilterPanel) {
			Show(panel, "PyreRows", pyre);  // D2R shows a panel's widgets on open, the layout's "visible": false too
			D2RL::Widgets::WidgetHandle native {};
			if (pyre && g_widgets->findPanel(g_context, "LootFilterProfilePanel", &native) == D2RL::Widgets::Result::Success) {
				g_widgets->setWidgetVisible(g_context, native, false);
				g_widgets->setWidgetEnabled(g_context, native, false);
			}
		}
		// shortcut: a panel D2R keeps loaded after it closes reads as still open, so the owners refresh on the
		// first open only (their clicks here keep the rows right); a chat or toml change shows after a restart.
		if (!g_panelSeen[p]) {
			g_panelSeen[p] = true;
			RefreshAll();
			Dirty();
		}
		if (!g_dirty[p].exchange(false)) {
			continue;
		}
		try {
			const std::lock_guard guard(g_lock);
			for (Row& row : g_rows) {
				if (settings_rows::PanelOf(row.name) != Panels[p]) {
					continue;
				}
				if (!row.dropdown) {
					Show(panel, settings_rows::ToggleWidget(row.name, true), !row.removed && row.value);
					continue;
				}
				const settings_rows::Dropdown* d = settings_rows::FindDropdown(row.name);
				const bool valid = row.index >= 0 && static_cast<size_t>(row.index) < row.options.size();
				uint8_t* widget = d != nullptr && !row.removed ? NativeDropdown(*d) : nullptr;
				if (widget == nullptr) {
					continue;
				}
				const int32_t state = valid ? settings_rows::StateOf(*d, row.options[row.index]) : -1;
				if (state >= 0) {  // a no-op when it already shows that state
					reinterpret_cast<void (*)(void*, int32_t) noexcept>(DropdownSelect.address)(widget, state);
				}
				row.shown = *reinterpret_cast<int32_t*>(widget + DropdownSelected);
			}
		} catch (...) {
			g_dirty[p] = true;
		}
	}
}

// A dropdown pick: its onUpdateMessage (PanelManager:OpenPanel) carries the picked index as a payload in place of
// the text, so it names no row; each dropdown whose picked state differs from its row's is the one picked.
auto OnPick() noexcept -> D2RL::SharedEvents::UiMessageAction {
	struct Apply {
		abi::IntAppliedFn onIndex;
		int32_t index;
		void* userData;
	};
	std::vector<Apply> applies;
	try {
		const std::lock_guard guard(g_lock);
		for (Row& row : g_rows) {
			const settings_rows::Dropdown* d = row.dropdown && !row.removed ? settings_rows::FindDropdown(row.name) : nullptr;
			uint8_t* widget = d != nullptr ? NativeDropdown(*d) : nullptr;
			if (widget == nullptr) {
				continue;
			}
			const int32_t picked = *reinterpret_cast<int32_t*>(widget + DropdownSelected);
			// compared with what it showed, not with the owner's option: that may have no state, or be a change not yet shown
			if (picked == row.shown) {
				continue;
			}
			row.shown = picked;
			const int32_t at = settings_rows::OptionOf(*d, picked, row.options);
			Dirty();  // a pick the owner does not offer goes back to the owner's
			if (at >= 0) {
				row.index = at;
				applies.push_back({row.onIndex, at, row.userData});
			}
		}
	} catch (...) {
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}
	if (applies.empty()) {
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}
	g_clicks += static_cast<uint32_t>(applies.size());
	// the owner saves its toml and may call setDropdownOptions back: no lock held
	for (const Apply& apply : applies) {
		if (apply.onIndex != nullptr) {
			apply.onIndex(apply.index, apply.userData);
		}
	}
	return D2RL::SharedEvents::UiMessageAction::Consume;
}

auto OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
	-> D2RL::SharedEvents::UiMessageAction {
	constexpr size_t PrefixSize = sizeof(settings_rows::ClickPrefix) - 1;
	if (event == nullptr || event->target == nullptr || event->command == nullptr) {
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}
	if (std::strcmp(event->target, "PausePanelMessage") == 0 && std::strcmp(event->command, "OpenLootFilter") == 0) {
		g_soon = true;  // D2R opens it after us: hide its own screen on the next frame, not up to WatchFrames later
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}
	if (std::strcmp(event->target, "PanelManager") != 0 || std::strcmp(event->command, "OpenPanel") != 0) {
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}
	if (event->text == nullptr) {
		return OnPick();
	}
	if (std::strncmp(event->text, settings_rows::ClickPrefix, PrefixSize) != 0) {
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}
	const char* clicked = event->text + PrefixSize;  // a toggle row
	abi::ToggleAppliedFn onToggle = nullptr;
	void* userData = nullptr;
	bool value = false;
	{
		const std::lock_guard guard(g_lock);
		for (Row& row : g_rows) {
			if (!row.removed && !row.dropdown && row.name == clicked) {
				row.value = value = !row.value;
				onToggle = row.onToggle;
				userData = row.userData;
				Dirty();
				break;
			}
		}
	}
	++g_clicks;
	if (onToggle != nullptr) {  // the owner saves its toml: no lock held
		onToggle(value, userData);
	}
	return D2RL::SharedEvents::UiMessageAction::Consume;
}

template <class Service>
auto Query(const Service*& service, bool (*has)(const Service*, uint32_t), uint32_t required) noexcept -> bool {
	if (g_context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !has(service, required)) {
		service = nullptr;
	}
	return service != nullptr;
}

void InstallHost(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	const D2RL::PluginCommunicationService* comms = nullptr;
	const D2RL::SharedEventService* events = nullptr;
	const D2RL::OverlayService* overlay = nullptr;
	const bool widgets = Query(g_widgets, D2RL::HasWidgetServiceField, D2RL::WidgetServiceRequiredSize);
	const bool shared = Query(events, D2RL::HasSharedEventServiceField, D2RL::SharedEventServiceRequiredSize);
	const bool frames = Query(overlay, D2RL::HasOverlayServiceField, D2RL::OverlayServiceRequiredSize);
	const bool comm = Query(comms, D2RL::HasPluginCommunicationServiceField, D2RL::PluginCommunicationServiceRequiredSize);
	if (!(widgets && shared && frames && comm)) {
		std::snprintf(report, size, "FAILED (service missing: widget %d, event %d, overlay %d, communication %d)", widgets,
		              shared, frames, comm);
		return;
	}
	sigscan::Resolve(Sigs, std::size(Sigs), context->exeBase, nullptr, 0);
	g_native = true;
	for (const sigscan::Entry* e : Sigs) {
		if (e->state != sigscan::State::Ok) {
			g_native = false;
			plog::Info("signature %s %s: the dropdown rows are off", e->name, sigscan::StateText(e->state));
		}
	}
	sigscan::Resolve(YieldSigs, std::size(YieldSigs), context->exeBase, nullptr, 0);
	g_yield = true;
	for (const sigscan::Entry* e : YieldSigs) {
		if (e->state != sigscan::State::Ok) {
			g_yield = false;
			plog::Info("signature %s %s: D2R's own loot filter screen stays, Pyre's rows are off", e->name,
			           sigscan::StateText(e->state));
		}
	}
	const D2RL::SharedEvents::UiMessageListener listener {.structSize = D2RL::SharedEvents::UiMessageListenerSize,
	                                                      .callback = OnUiMessage};
	const D2RL::Overlay::CallbackRegistration frame {.structSize = D2RL::Overlay::CallbackRegistrationSize,
	                                                 .phase = D2RL::Overlay::Phase::AfterGameUi,
	                                                 .callback = OnFrame};
	D2RL::SharedEvents::ListenerHandle listenerHandle = D2RL::SharedEvents::InvalidHandle;
	D2RL::Overlay::CallbackHandle frameHandle = D2RL::Overlay::InvalidCallbackHandle;
	if (events->registerUiMessageListener(context, &listener, &listenerHandle) != D2RL::SharedEvents::Result::Success ||
	    overlay->registerFrameCallback(context, &frame, &frameHandle) != D2RL::Overlay::Result::Success) {
		std::snprintf(report, size, "FAILED (a listener was refused)");
		return;
	}
	const D2RL::PluginCommunication::PublishServiceRequest publish {
		.structSize = D2RL::PluginCommunication::PublishServiceRequestSize,
		.name = "settings",
		.serviceVersion = Service.serviceVersion,
		.tableSize = Service.serviceSize,
		.table = &Service,
	};
	const auto published = comms->publishService(context, &publish);
	if (published != D2RL::PluginCommunication::Result::Success) {
		std::snprintf(report, size, "FAILED (publishing the settings service: result %u)", static_cast<unsigned>(published));
		return;
	}
	g_comms = comms;  // the first frame publishes "ready"
	std::snprintf(report, size, "the settings service is up: companion rows show in Options > Gameplay, Pyre's in Esc > Loot Filter");
}

parts::Part g_parts[] = {
	{"host", true, InstallHost},
};

constexpr D2RL::PluginInfo Info {
	.infoSize = D2RL::PluginInfoSize,
	.abiVersion = D2RL_PLUGIN_ABI_VERSION,
	.id = MODKIT_PLUGIN_NAME,  // the provider id the companions acquire
	.name = "PD2 Resurrected Settings Host",
	.version = MODKIT_PLUGIN_VERSION,
	.author = "Synpoo",
	.description = "Shows the companions' settings rows in Options > Gameplay, and Pyre Loot Filter's in Esc > Loot Filter.",
	.flags = D2RL::PluginFlags::Client,
};

auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
	-> D2RL::ConsoleCommandResult {
	if (command == nullptr || command->plugin == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	parts::WriteStatus(g_parts, std::size(g_parts), command);
	char line[200];
	size_t rows = 0;
	{
		const std::lock_guard guard(g_lock);
		for (const Row& row : g_rows) {
			rows += row.removed ? 0 : 1;
		}
	}
	std::snprintf(line, sizeof(line), "%s %s: %zu rows, %u clicks", PluginName, PluginVersion, rows, g_clicks.load());
	command->plugin->WriteConsoleMessage(line);
	return D2RL::ConsoleCommandResult::Handled;
}

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}
	g_context = context;
	plog::Init(context, PluginName);
	crash_guard::Install(PluginVersion, context->exeBase);
	char image[200];
	build_guard::Describe(image, sizeof(image), build_guard::ImageStamp(static_cast<uintptr_t>(context->exeBase)),
	                      D2RL::GetBuildVersion(context));
	plog::Info("%s loaded (mod %s, %s)", PluginVersion, context->activeMod != nullptr ? context->activeMod : "?", image);
	off_switch::Read(context->pluginConfigPath);
	parts::InstallAll(g_parts, std::size(g_parts), context, PluginName, d2r::Bind(context->exeBase));
	return context->RegisterConsoleCommand(PluginName, StatusCommand, "PD2 Resurrected Settings Host: its rows and clicks.");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	crash_guard::Remove();
	g_context = nullptr;
}
