// Lane D ubers: the parts and status counters this lane installs, registered by order (parts.h; #943).
// A lane adds its parts here, in its own folder, and never edits plugin.cpp. Orders are one global sequence.
#include "part_registry.h"

#include "arena_spots.h"
#include "dclone_ai.h"
#include "lucion.h"
#include "lucion_ai.h"
#include "lucion_chests.h"
#include "prime_minions.h"
#include "rathma.h"
#include "rathma_ai.h"
#include "rathma_life.h"
#include "uber_drops.h"
#include "uber_entry.h"
#include "uber_tristram.h"

#include <cstdio>

namespace {

void InstallLucionPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallLucion(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

void InstallPrimesPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallPrimeMinions(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

void InstallTemplePart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallLucionChests(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

void InstallLucionaiPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallLucionAi(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

void InstallRathmaaiPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallRathmaAi(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

void InstallRathmalifePart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallRathmaLife(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 2, rules);
}

void InstallDcloneaiPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallDcloneAi(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 2, rules);
}

void InstallArenaPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	InstallArenaSpots(context, report, size);
}

void InstallUberkeyPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	InstallUberKey(context, report, size);
}

PartReg r70(70, "trist", InstallTristram);
PartReg r156(156, "uberkey", InstallUberkeyPart);
PartReg r57(57, "arena", InstallArenaPart);
PartReg r80(80, "lucion", InstallLucionPart);
PartReg r90(90, "primes", InstallPrimesPart);
PartReg r100(100, "temple", InstallTemplePart);
PartReg r120(120, "lucionai", InstallLucionaiPart);
PartReg r130(130, "rathmaai", InstallRathmaaiPart);
PartReg r140(140, "rathmalife", InstallRathmalifePart);
PartReg r150(150, "dcloneai", InstallDcloneaiPart);
CountersReg c60(60, LucionCounters);
CountersReg c70(70, PrimeMinionsCounters);
CountersReg c80(80, LucionChestsCounters);
CountersReg c90(90, UberDropsCounters);
CountersReg c100(100, RathmaCounters);
CountersReg c110(110, TristramCounters);
CountersReg c160(160, LucionAiCounters);
CountersReg c170(170, RathmaAiCounters);
CountersReg c180(180, RathmaLifeCounters);
CountersReg c190(190, DcloneAiCounters);
CountersReg c196(196, UberKeyCounters);

}  // namespace
