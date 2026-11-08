// core-ui: PD2's client-side engine features no companion covers, for PD2 Resurrected. Client-only (not Shared: a UI
// change must never force host and client to match) and NativeHooks; a game-wide plugin in <game>\d2rloader\plugins
// (ADR 0004). On a game image other than the one in rvas_*.h it loads, logs the stamp and leaves every part off.
//
// Parts, by their word for the log, the status command and core-ui-off.txt (<game>\d2rloader\config\core-ui-off.txt):
//   automap   the automap for PD2's level types 36+ (LP2)
//   pictures  HD pictures of PD2's 4-character item codes (#310, #298), and `cheat spawn` of them (#323)
//   split     PD2's stack split: Ctrl+left-click with a stack held puts one item down (#316)
//   quantity  PD2's Show Item Quantity: each stack in an open grid shows its quantity on its picture (#359)
//   palette   legacy graphics: a level entered without a load screen draws in its own palette (#296)
#include <D2RLPlugin/api.h>

#include "automap.h"
#include "crash_guard.h"
#include "pd2_engine.h"
#include "item_pictures.h"
#include "item_quantity.h"
#include "level_palette.h"
#include "log.h"
#include "off_switch.h"
#include "stack_split.h"

#include <cstdio>

#ifndef CORE_PLUGIN_NAME
#define CORE_PLUGIN_NAME "core-ui"
#endif
#ifndef CORE_PLUGIN_VERSION
#define CORE_PLUGIN_VERSION "0.2.0"
#endif

namespace {

constexpr const char* PluginName = CORE_PLUGIN_NAME;
constexpr const char* PluginVersion = CORE_PLUGIN_VERSION;

const D2RL::PluginContext* g_context = nullptr;
char g_selfPath[520] = "?";
char g_automap[300] = "";
char g_pictures[200] = "";
char g_split[200] = "";
char g_quantity[200] = "";
char g_palette[200] = "";

constexpr D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = CORE_PLUGIN_NAME,
	.name        = "PD2 Resurrected Core UI",
	.version     = CORE_PLUGIN_VERSION,
	.author      = "Synpoo",
	.description = "Project Diablo II client features (the automap of PD2's new level types, HD pictures of its 4-character item codes, its stack split, its item quantity display, legacy level palettes) for PD2 Resurrected.",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

auto StatusCommand(D2R::Game::Client* client, const D2RL::ConsoleCommandContext* command, void* userData) noexcept
	-> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;
	if (command == nullptr || command->plugin == nullptr || g_context == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	char line[600];
	char image[200];
	build_guard::Describe(image, sizeof(image), build_guard::ImageStamp(static_cast<uintptr_t>(g_context->exeBase)), D2RL::GetBuildVersion(g_context));
	std::snprintf(line, sizeof(line), "%s %s | %s | mod %s | file %s", PluginName, PluginVersion, image,
	              g_context->activeMod != nullptr ? g_context->activeMod : "?", g_selfPath);
	command->plugin->WriteConsoleMessage(line);
	if (off_switch::Words()[0] != '\0') {
		std::snprintf(line, sizeof(line), "off-file: %s", off_switch::Words());
		command->plugin->WriteConsoleMessage(line);
	}
	std::snprintf(line, sizeof(line), "automap %s", g_automap);
	command->plugin->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "pictures %s", g_pictures);
	command->plugin->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "split %s", g_split);
	command->plugin->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "quantity %s", g_quantity);
	command->plugin->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "palette %s", g_palette);
	command->plugin->WriteConsoleMessage(line);
	automap::Counters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	stack_split::Counters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	item_quantity::Counters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	level_palette::Counters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
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
	if (D2RL::GetPluginPath(context) != nullptr) {
		std::snprintf(g_selfPath, sizeof(g_selfPath), "%ls", D2RL::GetPluginPath(context));
	}
	char image[200];
	build_guard::Describe(image, sizeof(image), build_guard::ImageStamp(static_cast<uintptr_t>(context->exeBase)), D2RL::GetBuildVersion(context));
	plog::Info("%s loaded (mod %s, %s, file %s)", PluginVersion, context->activeMod != nullptr ? context->activeMod : "?",
	           image, g_selfPath);
	off_switch::Read(context->pluginConfigPath);
	if (off_switch::Words()[0] != '\0') {
		plog::Info("%s-off.txt leaves out: %s", PluginName, off_switch::Words());
	}
	const bool bound = d2r::Bind(context->exeBase);
	if (!bound) {
		std::snprintf(g_automap, sizeof(g_automap), "off: unknown game image 0x%08x (expects 0x%08x)",
		              build_guard::ImageStamp(static_cast<uintptr_t>(context->exeBase)), d2r::rva::ImageStamp);
		std::snprintf(g_pictures, sizeof(g_pictures), "%s", g_automap);
	} else if (off_switch::Off("automap")) {
		std::snprintf(g_automap, sizeof(g_automap), "off: left out by %s-off.txt", PluginName);
	} else {
		automap::Install(context, g_automap, sizeof(g_automap));
	}
	plog::Info("automap %s", g_automap);
	if (bound && off_switch::Off("pictures")) {
		std::snprintf(g_pictures, sizeof(g_pictures), "off: left out by %s-off.txt", PluginName);
	} else if (bound) {
		item_pictures::Install(context, g_pictures, sizeof(g_pictures));
	}
	plog::Info("pictures %s", g_pictures);
	// SDK only (no RVA), so it runs on any game image
	if (off_switch::Off("split")) {
		std::snprintf(g_split, sizeof(g_split), "off: left out by %s-off.txt", PluginName);
	} else {
		stack_split::Install(context, g_split, sizeof(g_split));
	}
	plog::Info("split %s", g_split);
	// a native hook (the SDK cannot place or draw it on D2RLoader 1.3.1, #359), so it needs the known image
	if (!bound) {
		std::snprintf(g_quantity, sizeof(g_quantity), "%s", g_pictures);
	} else if (off_switch::Off("quantity")) {
		std::snprintf(g_quantity, sizeof(g_quantity), "off: left out by %s-off.txt", PluginName);
	} else {
		item_quantity::Install(context, g_quantity, sizeof(g_quantity));
	}
	plog::Info("quantity %s", g_quantity);
	if (!bound) {
		std::snprintf(g_palette, sizeof(g_palette), "%s", g_pictures);
	} else if (off_switch::Off("palette")) {
		std::snprintf(g_palette, sizeof(g_palette), "off: left out by %s-off.txt", PluginName);
	} else {
		level_palette::Install(context, g_palette, sizeof(g_palette));
	}
	plog::Info("palette %s", g_palette);
	return context->RegisterConsoleCommand(PluginName, StatusCommand, "PD2 Resurrected Core UI: its parts and counters.");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	crash_guard::Remove();
	g_context = nullptr;
}
