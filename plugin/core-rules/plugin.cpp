// core-rules: PD2's engine rules (skill and hit functions, cube ops, item rules, server hooks) for PD2 Resurrected.
// Shared (host and client must match) and NativeHooks; a mod-scoped plugin in <game>\mods\<mod>\d2rloader\plugins (ADR 0004, #1180).
// On a game image other than the one in rvas_*.h it loads, logs the stamp and leaves every part off.
//
// Parts: each has a one-word name for the log, the status command and core-rules-off.txt
// (<game>\mods\<mod>\d2rloader\config\core-rules-off.txt), which leaves listed parts out to bisect a crash.
#include <D2RLPlugin/api.h>
#include <D2RLPlugin/lifecycle.h>

#include "../core-events/arena_spots.h"  // the "arena" part: Royal Fire reads the arena centre (#962)
#include "almanac.h"
#include "boss_aura.h"
#include "shrine_rules.h"
#include "crash_guard.h"
#include "property_ids.h"
#include "cube_rules.h"
#include "cube_stacks.h"
#include "item_prices.h"
#include "stash_stacks.h"
#include "pd2_engine.h"
#include "item_rules.h"
#include "log.h"
#include "loot_rules.h"
#include "merc_rules.h"
#include "socket_rules.h"
#include "off_switch.h"
#include "parts.h"
#include "skill_functions.h"
#include "summon_ai.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

#ifndef MODKIT_PLUGIN_NAME
#define MODKIT_PLUGIN_NAME "core-rules"
#endif
#ifndef MODKIT_PLUGIN_VERSION
#define MODKIT_PLUGIN_VERSION "0.2.4"
#endif

namespace {

constexpr const char* PluginName = MODKIT_PLUGIN_NAME;
constexpr const char* PluginVersion = MODKIT_PLUGIN_VERSION;

const D2RL::PluginContext* g_context = nullptr;
char g_selfPath[520] = "?";
bool g_imageKnown = false;

constexpr D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = MODKIT_PLUGIN_NAME,
	.name        = "PD2 Resurrected Core Rules",
	.version     = MODKIT_PLUGIN_VERSION,
	.author      = "Synpoo",
	.description = "Project Diablo II engine rules (skill functions and server rules) for PD2 Resurrected.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

void InstallSkills(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char slots[2600] = "";
	const int installed = InstallSkillFunctions(context, slots, sizeof(slots));
	std::snprintf(report, size, "%d/%d installed:%s", installed, SkillFunctionCount(), slots);
}

void InstallItems(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[600] = "";
	const int installed = InstallItemRules(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, ItemRuleCount(), rules);
}

void InstallCube(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallCubeRules(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, CubeRuleCount(), rules);
}

void InstallAlmanacPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallAlmanac(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/1 installed: %s", installed, rules);
}

void InstallShrinesPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[300] = "";
	const int installed = InstallShrines(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/1 installed: %s", installed, rules);
}

void InstallStacks(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "", stash[100] = "";
	const int installed = InstallCubeStacks(context, rules, sizeof(rules)) + (InstallStashStacks(context, stash, sizeof(stash)) ? 1 : 0);
	std::snprintf(report, size, "%d/2 installed: %s, %s", installed, rules, stash);
}

void InstallPrices(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	InstallItemPrices(context, report, size);
}

void InstallMercs(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallMercRules(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, MercRuleCount(), rules);
}

void InstallSockets(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallSocketRules(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, SocketRuleCount(), rules);
}

void InstallLoot(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallLootRules(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, LootRuleCount(), rules);
}

void InstallSummons(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[256] = "";
	const int installed = InstallSummonAi(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, SummonAiCount(), rules);
}

void InstallArena(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	InstallArenaSpots(context, report, size);
}

parts::Part g_parts[] = {
	{"skills", true, InstallSkills},
	{"arena", true, InstallArena},
	{"items", true, InstallItems},
	{"mercs", true, InstallMercs},
	{"cube", true, InstallCube},
	{"stacks", true, InstallStacks},
	{"prices", true, InstallPrices},
	{"almanac", true, InstallAlmanacPart},
	{"shrines", true, InstallShrinesPart},
	{"loot", true, InstallLoot},
	{"sockets", true, InstallSockets},
	{"summons", true, InstallSummons},
	{"bosses", true, InstallBossAura, BossAuraState, BossAuraUses},
};

// Cast Triggers allocates its 5 relay pages upward from the image (within +0x70000000) when tables load, and D2R's
// render-context init can fill that window first (#1180). Hold the first 16 free 64 KB blocks from plugin load and
// free them in a table listener that runs before Cast Triggers' (mod-folder plugins load in file-name
// order: core-rules.dll before d2rl-ruffneckk-cast-triggers.dll).
// Not a part: the off file cannot drop it.
void* g_reserved[16] = {};

void ReserveNearImage(uint64_t image) noexcept {
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
	const uint64_t step = 0x10000;
	const uint64_t end = image + 0x70000000ull;
	int held = 0;
	for (uint64_t at = (image + nt->OptionalHeader.SizeOfImage + step - 1) & ~(step - 1); at < end && held < 16; at += step) {
		if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), step, MEM_RESERVE, PAGE_NOACCESS)) {
			g_reserved[held++] = p;
		}
	}
	plog::Info("relay reserve: %d blocks held above the image, first %p, last %p", held, g_reserved[0],
	           held > 0 ? g_reserved[held - 1] : nullptr);
}

void __cdecl ReleaseReserve(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	int freed = 0;
	for (void*& p : g_reserved) {
		if (p != nullptr && VirtualFree(p, 0, MEM_RELEASE)) {
			p = nullptr;
			++freed;
		}
	}
	if (freed > 0) {
		plog::Info("relay reserve: %d blocks released for Cast Triggers", freed);
	}
}

void HoldRelayReserve(const D2RL::PluginContext* context) noexcept {
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
	    !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		plog::Warn("relay reserve: no lifecycle service, nothing held");
		return;
	}
	const D2RL::Lifecycle::DataTablesLoadedListener listener {.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
	                                                          .callback = ReleaseReserve};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	if (lifecycle->registerDataTablesLoadedListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
		plog::Warn("relay reserve: table listener refused, nothing held");
		return;
	}
	ReserveNearImage(context->exeBase);  // only once the listener that frees it is in place
}

auto StatusCommand(D2R::Game::Client* client, const D2RL::ConsoleCommandContext* command, void* userData) noexcept
	-> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;
	if (command == nullptr || command->plugin == nullptr || g_context == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	const char* args = command->args != nullptr ? command->args : "";
	while (*args == ' ') {
		++args;
	}
	if (std::strncmp(args, "armageddon", 10) == 0 && (args[10] == '\0' || args[10] == ' ')) {
		char why[120];
		const bool queued = QueueArmageddon(why, sizeof(why));
		char answer[160];
		std::snprintf(answer, sizeof(answer), "%s: %s", PluginName, why);
		command->plugin->WriteConsoleMessage(answer);
		return queued ? D2RL::ConsoleCommandResult::Handled : D2RL::ConsoleCommandResult::Failed;
	}
	char line[2500];
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
	SkillFunctionCounters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);  // also in the plugin log, for game checks and crash reports
	ItemRuleCounters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	MercRuleCounters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	CubeRuleCounters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	CubeStackCounters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	StashStackCounters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	ItemPriceCounters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	AlmanacCounters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	ShrineCounters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	LootRuleCounters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	SocketRuleCounters(line, sizeof(line));
	command->plugin->WriteConsoleMessage(line);
	plog::Info("%s", line);
	SummonAiCounters(line, sizeof(line));
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
	HoldRelayReserve(context);  // first, before any part allocates near the image
	InstallMercGear(context);   // not a part: the off file cannot drop it, or saves would lose merc items (#1627)
	propids::Init(context);
	g_imageKnown = d2r::Bind(context->exeBase);
	parts::InstallAll(g_parts, std::size(g_parts), context, PluginName, g_imageKnown);
	return context->RegisterConsoleCommand(PluginName, StatusCommand,
	                                       "PD2 Resurrected Core Rules: every part and its counters; `armageddon` (dev) turns a Phase Blade into the Armageddon Blade.");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	crash_guard::Remove();
	g_context = nullptr;
}
