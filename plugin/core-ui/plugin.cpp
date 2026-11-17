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
#include "parts.h"
#include "stack_split.h"

#include <cstdio>
#include <iterator>

#ifndef MODKIT_PLUGIN_NAME
#define MODKIT_PLUGIN_NAME "core-ui"
#endif
#ifndef MODKIT_PLUGIN_VERSION
#define MODKIT_PLUGIN_VERSION "0.2.0"
#endif

namespace {

constexpr const char* PluginName = MODKIT_PLUGIN_NAME;
constexpr const char* PluginVersion = MODKIT_PLUGIN_VERSION;

const D2RL::PluginContext* g_context = nullptr;
char g_selfPath[520] = "?";

// pictures, quantity and palette are native hooks (the SDK cannot place or draw them on D2RLoader 1.3.1, #359): they need
// the known image. split is SDK only (no RVA), so it runs on any game image.
parts::Part g_parts[] = {
	{"automap", true, automap::Install},
	{"pictures", true, item_pictures::Install},
	{"split", false, stack_split::Install},
	{"quantity", true, item_quantity::Install},
	{"palette", true, level_palette::Install},
};

constexpr D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = MODKIT_PLUGIN_NAME,
	.name        = "PD2 Resurrected Core UI",
	.version     = MODKIT_PLUGIN_VERSION,
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
	parts::WriteStatus(g_parts, std::size(g_parts), command);
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
	parts::InstallAll(g_parts, std::size(g_parts), context, PluginName, d2r::Bind(context->exeBase));
	return context->RegisterConsoleCommand(PluginName, StatusCommand, "PD2 Resurrected Core UI: its parts and counters.");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	crash_guard::Remove();
	g_context = nullptr;
}
