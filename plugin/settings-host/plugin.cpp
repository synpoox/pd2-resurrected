// aa-settings-host: the settings host Pyre-Flies' companions look for (provider plugin id "aa-settings-host", service
// "settings" v1, event "ready" v1). The id is theirs and must match. Load order does not matter:
// a client that loads first waits for "ready". No public plugin provides it, so without this one their Options rows never show (#2237).
// Client-only, SDK only (no game address, no hook); it still keeps the ADR 0004 image gate.
//
// D2RLoader 1.3.1's WidgetService ends at getInputText: no widget text can change and no dropdown or toggle can be
// read or set. So the rows are static layout rows (gameplayoptionspanelhd.json, names from rows.h) that this plugin
// only shows and hides. A toggle row is a checkbox button with the ticked art over it; a dropdown row is one button
// with a text per known option label over it, and a click steps to the next option. Sliders and status rows need runtime text: refused (Unavailable), they stay toml-only.
// A click applies at once (the Options panel's own Apply button does not cover plugin rows).
#include <D2RLPlugin/api.h>

#include "crash_guard.h"
#include "log.h"
#include "off_switch.h"
#include "parts.h"
#include "pd2_engine.h"
#include "rows.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#ifndef MODKIT_PLUGIN_NAME
#define MODKIT_PLUGIN_NAME "aa-settings-host"
#endif
#ifndef MODKIT_PLUGIN_VERSION
#define MODKIT_PLUGIN_VERSION "0.2.6"
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
constexpr const char* OptionsPanel = "GameplayOptionsPanel";
constexpr uint32_t WatchFrames = 6;  // the padlock's pace: a few frames after a click or an open is soon enough

struct Row {
	std::string name;  // settings_rows::RowName
	bool dropdown = false;
	bool removed = false;
	bool value = false;  // toggle
	int32_t index = 0;   // dropdown
	std::vector<std::string> options;
	abi::ToggleAppliedFn onToggle = nullptr;
	abi::IntAppliedFn onIndex = nullptr;
	abi::RefreshToggleFn refreshToggle = nullptr;
	abi::RefreshIntFn refreshIndex = nullptr;
	void* userData = nullptr;
};

const D2RL::PluginContext* g_context = nullptr;
const D2RL::WidgetService* g_widgets = nullptr;
std::mutex g_lock;
std::deque<Row> g_rows;  // handle = index + 1; a deque keeps the rows in place as more are added
std::atomic<uint32_t> g_frames {0}, g_clicks {0};
std::atomic<bool> g_dirty {true};
bool g_panelSeen = false;  // UI thread only

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
	g_rows.push_back(std::move(row));
	*handle = g_rows.size();
	g_dirty = true;
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
		g_dirty = true;
	}
}

auto __cdecl SetToggle(abi::RowHandle handle, bool value) noexcept -> abi::Result {
	const std::lock_guard guard(g_lock);
	Row* row = Find(handle);
	if (row == nullptr || row->dropdown) {
		return abi::Result::InvalidArgument;
	}
	row->value = value;
	g_dirty = true;
	return abi::Result::Success;
}

auto __cdecl SetDropdown(abi::RowHandle handle, int32_t index) noexcept -> abi::Result {
	const std::lock_guard guard(g_lock);
	Row* row = Find(handle);
	if (row == nullptr || !row->dropdown) {
		return abi::Result::InvalidArgument;
	}
	row->index = index;
	g_dirty = true;
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
		g_dirty = true;
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
	if (++g_frames % WatchFrames != 0) {
		return;
	}
	D2RL::Widgets::WidgetHandle panel {};
	if (g_widgets->findPanel(g_context, OptionsPanel, &panel) != D2RL::Widgets::Result::Success) {
		g_panelSeen = false;
		return;
	}
	// shortcut: an Options panel D2R keeps loaded after it closes reads as still open, so the owners refresh on the
	// first open only (their clicks here keep the rows right); a chat or toml change shows after a restart.
	if (!g_panelSeen) {
		g_panelSeen = true;
		RefreshAll();
		g_dirty = true;
	}
	if (!g_dirty.exchange(false)) {
		return;
	}
	try {
		const std::lock_guard guard(g_lock);
		for (const Row& row : g_rows) {
			if (!row.dropdown) {
				Show(panel, settings_rows::ToggleWidget(row.name, true), !row.removed && row.value);
				continue;
			}
			bool known = false;
			for (size_t i = 0; i < row.options.size(); ++i) {
				D2RL::Widgets::WidgetHandle widget {};
				const std::string name = settings_rows::OptionWidget(row.name, row.options[i]);
				const bool chosen = !row.removed && static_cast<int32_t>(i) == row.index;
				if (g_widgets->findWidget(g_context, panel, name.c_str(), &widget) == D2RL::Widgets::Result::Success) {
					known = known || chosen;
					Show(panel, name, chosen);
				}
			}
			Show(panel, settings_rows::OptionWidget(row.name, "?"), !row.removed && !known);
		}
	} catch (...) {
		g_dirty = true;
	}
}

auto OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
	-> D2RL::SharedEvents::UiMessageAction {
	constexpr size_t PrefixSize = sizeof(settings_rows::ClickPrefix) - 1;
	if (event == nullptr || event->target == nullptr || event->command == nullptr || event->text == nullptr ||
	    std::strcmp(event->target, "PanelManager") != 0 || std::strcmp(event->command, "OpenPanel") != 0 ||
	    std::strncmp(event->text, settings_rows::ClickPrefix, PrefixSize) != 0) {
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}
	const char* name = event->text + PrefixSize;
	abi::ToggleAppliedFn onToggle = nullptr;
	abi::IntAppliedFn onIndex = nullptr;
	void* userData = nullptr;
	bool value = false;
	int32_t index = 0;
	{
		const std::lock_guard guard(g_lock);
		for (Row& row : g_rows) {
			if (row.removed || row.name != name) {
				continue;
			}
			if (row.dropdown) {
				row.index = index = settings_rows::NextIndex(row.index, static_cast<uint32_t>(row.options.size()));
				onIndex = row.onIndex;
			} else {
				row.value = value = !row.value;
				onToggle = row.onToggle;
			}
			userData = row.userData;
			g_dirty = true;
			break;
		}
	}
	++g_clicks;
	// the owner saves its toml and may call setDropdownOptions back: no lock held
	if (onIndex != nullptr) {
		onIndex(index, userData);
	} else if (onToggle != nullptr) {
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
	// clients that loaded first subscribed to this and attach now
	const D2RL::PluginCommunication::PublishEventRequest ready {
		.structSize = D2RL::PluginCommunication::PublishEventRequestSize,
		.name = "ready",
		.eventVersion = 1,
	};
	(void)comms->publishEvent(context, &ready);
	std::snprintf(report, size, "the settings service is up: companion toggles and dropdowns show in Options > Gameplay");
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
	.description = "Shows the companions' settings rows (Pyre Loot Filter's profile, its toggles) in Options > Gameplay.",
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
