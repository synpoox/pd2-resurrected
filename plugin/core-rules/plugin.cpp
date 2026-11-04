// core-rules: PD2's engine rules (skill and hit functions, cube ops, item rules, server hooks) for PD2 Resurrected.
// Shared (host and client must match) and NativeHooks; a game-wide plugin in <game>\d2rloader\plugins (ADR 0004).
// On a game image other than the one in rvas_*.h it loads, logs the stamp and leaves every part off.
//
// Parts: each has a one-word name for the log, the status command and core-rules-off.txt
// (<game>\d2rloader\config\core-rules-off.txt), which leaves listed parts out to bisect a crash.
#include <D2RLPlugin/api.h>

#include "crash_guard.h"
#include "d2r.h"
#include "item_rules.h"
#include "log.h"
#include "off_switch.h"
#include "skill_functions.h"

#include <cstdint>
#include <cstdio>

#ifndef CORE_PLUGIN_NAME
#define CORE_PLUGIN_NAME "core-rules"
#endif
#ifndef CORE_PLUGIN_VERSION
#define CORE_PLUGIN_VERSION "0.1.0"
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
	.id          = CORE_PLUGIN_NAME,
	.name        = "PD2 Resurrected Core Rules",
	.version     = CORE_PLUGIN_VERSION,
	.author      = "Synpoo",
	.description = "Project Diablo II engine rules (skill functions and server rules) for PD2 Resurrected.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

using InstallFn = void (*)(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

struct Part {
	const char* word;
	InstallFn install;
	char report[2400];  // one entry per installed slot, so it grows with every rule
};

void InstallSkills(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char slots[2300] = "";
	const int installed = InstallSkillFunctions(context, slots, sizeof(slots));
	std::snprintf(report, size, "%d/%d installed:%s", installed, SkillFunctionCount(), slots);
}

void InstallItems(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[600] = "";
	const int installed = InstallItemRules(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, ItemRuleCount(), rules);
}

Part g_parts[] = {
	{"skills", InstallSkills, ""},
	{"items", InstallItems, ""},
};

void InstallParts(const D2RL::PluginContext* context) noexcept {
	for (Part& p : g_parts) {
		if (!g_imageKnown) {
			std::snprintf(p.report, sizeof(p.report), "off: unknown game image 0x%08x (expects 0x%08x)",
			              d2r::ImageStampOf(context->exeBase), d2r::rva::ImageStamp);
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
	char image[200];
	crash_guard::Describe(image, sizeof(image), D2RL::GetBuildVersion(g_context));
	std::snprintf(line, sizeof(line), "%s %s | %s | mod %s | file %s", PluginName, PluginVersion, image,
	              g_context->activeMod != nullptr ? g_context->activeMod : "?", g_selfPath);
	command->plugin->WriteConsoleMessage(line);
	if (off_switch::Words()[0] != '\0') {
		std::snprintf(line, sizeof(line), "off-file: %s", off_switch::Words());
		command->plugin->WriteConsoleMessage(line);
	}
	for (const Part& p : g_parts) {
		std::snprintf(line, sizeof(line), "%s %s", p.word, p.report);
		command->plugin->WriteConsoleMessage(line);
	}
	SkillFunctionCounters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);  // also in the plugin log, for game checks and crash reports
	ItemRuleCounters(line, sizeof(line));
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
	crash_guard::Describe(image, sizeof(image), D2RL::GetBuildVersion(context));
	plog::Info("%s loaded (mod %s, %s, file %s)", PluginVersion, context->activeMod != nullptr ? context->activeMod : "?",
	           image, g_selfPath);
	off_switch::Read(context->pluginConfigPath);
	if (off_switch::Words()[0] != '\0') {
		plog::Info("%s-off.txt leaves out: %s", PluginName, off_switch::Words());
	}
	g_imageKnown = d2r::Bind(context->exeBase);
	InstallParts(context);
	return context->RegisterConsoleCommand(PluginName, StatusCommand,
	                                       "PD2 Resurrected Core Rules: every part and its counters.");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	crash_guard::Remove();
	g_context = nullptr;
}
