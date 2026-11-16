// core-events: PD2's map events and uber fights for PD2 Resurrected (maps are Cartographer's): cube op 30, the map events, the
// uber keys' opening and drop rule, tier scaling, AI and drops, Horazon's portal, Lucion's death and the Temple's chests.
// Shared (host and client must match) and NativeHooks; a game-wide plugin in <game>\d2rloader\plugins (ADR 0004).
// On a game image other than the one in rvas_*.h it loads, logs the stamp and leaves every part off.
//
// Parts: registered by the lanes (parts.h); each has a one-word name for the log, the status command and core-events-off.txt
// (<game>\d2rloader\config\core-events-off.txt), which leaves listed parts out to bisect a crash.
#include <D2RLPlugin/api.h>

#include "ai_hooks.h"
#include "crash_guard.h"
#include "entity_pool.h"
#include "log.h"
#include "map_events.h"
#include "off_switch.h"
#include "parts.h"
#include "pd2_engine.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef CORE_PLUGIN_NAME
#define CORE_PLUGIN_NAME "core-events"
#endif
#ifndef CORE_PLUGIN_VERSION
#define CORE_PLUGIN_VERSION "0.2.0"
#endif

namespace {

constexpr const char* PluginName = CORE_PLUGIN_NAME;
constexpr const char* PluginVersion = CORE_PLUGIN_VERSION;

const D2RL::PluginContext* g_context = nullptr;
char g_selfPath[520] = "?";
bool g_imageKnown = false;

constexpr D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "core-maps", // saved characters record plugin ids; keep the pre-#1040 id so they load (#1044)
	.name        = "PD2 Resurrected Core Events",
	.version     = CORE_PLUGIN_VERSION,
	.author      = "Synpoo",
	.description = "Project Diablo II map events and uber fights (cube op 30, map events, uber keys, scaling and drops, Horazon's portal) for PD2 Resurrected.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

void InstallParts(const D2RL::PluginContext* context) noexcept {
	size_t count = 0;
	Part* parts = Parts(&count);
	if (RegistryOverflowed()) {
		plog::Warn("parts registry full: some parts or counters are not registered");
	}
	for (size_t i = 0; i < count; ++i) {
		Part& p = parts[i];
		if (!g_imageKnown) {
			std::snprintf(p.report, sizeof(p.report), "off: unknown game image 0x%08x (expects 0x%08x)",
			              build_guard::ImageStamp(static_cast<uintptr_t>(context->exeBase)), d2r::rva::ImageStamp);
		} else if (off_switch::Off(p.word)) {
			std::snprintf(p.report, sizeof(p.report), "off: left out by %s-off.txt", PluginName);
		} else {
			p.install(context, p.report, sizeof(p.report));
		}
		plog::Info("%s %s", p.word, p.report);
	}
}

auto StatusCommand(D2R::Game::Client* client, const D2RL::ConsoleCommandContext* command, void* userData) noexcept
	-> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;
	if (command == nullptr || command->plugin == nullptr || g_context == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	char line[2500];
	const char* args = command->args;
	while (args != nullptr && *args == ' ') {
		++args;
	}
	if (args != nullptr && std::strncmp(args, "life ", 5) == 0) {
		char* end = nullptr;
		const long percent = std::strtol(args + 5, &end, 10);
		const bool percentRead = end != args + 5;
		char* classEnd = nullptr;
		const long classId = std::strtol(end, &classEnd, 10);
		if (!percentRead || classEnd == end || percent < 0 || percent > 100 || classId <= 0) {
			std::snprintf(line, sizeof(line), "%s: usage: %s life <percent 0-100> <merged monstats class>", PluginName,
			              PluginName);
			command->plugin->WriteConsoleMessage(line);
			return D2RL::ConsoleCommandResult::Failed;
		}
		RequestBossLife(static_cast<int32_t>(percent), static_cast<int32_t>(classId));
		std::snprintf(line, sizeof(line), "%s: the next think of a ported boss of class %ld sets its life to %ld%%",
		              PluginName, classId, percent);
		command->plugin->WriteConsoleMessage(line);
		plog::Info("%s", line);
		return D2RL::ConsoleCommandResult::Handled;
	}
	if (args != nullptr && std::strncmp(args, "event ", 6) == 0) {
		const bool ok = ForceNextMapEvent(args + 6);
		std::snprintf(line, sizeof(line), ok ? "%s: the next map rolls event %s" : "%s: usage: %s event wanderer|mendeln|treasure|gheed|altar|spire|off",
		              PluginName, ok ? args + 6 : PluginName);
		command->plugin->WriteConsoleMessage(line);
		plog::Info("%s", line);
		return ok ? D2RL::ConsoleCommandResult::Handled : D2RL::ConsoleCommandResult::Failed;
	}
	char image[200];
	build_guard::Describe(image, sizeof(image), build_guard::ImageStamp(static_cast<uintptr_t>(g_context->exeBase)), D2RL::GetBuildVersion(g_context));
	std::snprintf(line, sizeof(line), "%s %s | %s | mod %s | file %s", PluginName, PluginVersion, image,
	              g_context->activeMod != nullptr ? g_context->activeMod : "?", g_selfPath);
	command->plugin->WriteConsoleMessage(line);
	if (off_switch::Words()[0] != '\0') {
		std::snprintf(line, sizeof(line), "off-file: %s", off_switch::Words());
		command->plugin->WriteConsoleMessage(line);
	}
	size_t count = 0;
	const Part* parts = Parts(&count);
	for (size_t i = 0; i < count; ++i) {
		const Part& p = parts[i];
		std::snprintf(line, sizeof(line), "%s %s", p.word, p.report);
		command->plugin->WriteConsoleMessage(line);
	}
	std::snprintf(line, sizeof(line), "pool now %s", EntityPoolState());
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	size_t counterCount = 0;
	const CountersFn* counters = Counters(&counterCount);
	for (size_t i = 0; i < counterCount; ++i) {
		counters[i](line, sizeof(line));
		command->plugin->WriteConsoleMessage(line);
		plog::Info("%s", line);
	}
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
	g_imageKnown = d2r::Bind(context->exeBase);
	InstallParts(context);
	return context->RegisterConsoleCommand(PluginName, StatusCommand,
	                                       "PD2 Resurrected Core Events: every part and its counters; `life <percent> <class>` sets a ported boss's life (test aid).");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	crash_guard::Remove();
	g_context = nullptr;
}
