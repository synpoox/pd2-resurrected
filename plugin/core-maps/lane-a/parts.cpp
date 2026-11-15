// Lane A native maps: the parts and status counters this lane installs, registered by order (parts.h; #943).
// A lane adds its parts here, in its own folder, and never edits plugin.cpp. Orders are one global sequence.
#include "parts.h"

#include "map_boss_ai.h"
#include "map_bosses.h"
#include "map_open.h"
#include "map_stats.h"
#include "unique_maps.h"
#include "zhar.h"

#include <cstdio>

namespace {

void InstallOpenPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallMapOpen(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, MapOpenCount(), rules);
}

void InstallStatsPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallMapStats(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, MapStatsCount(), rules);
}

void InstallMapbossaiPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallMapBossAi(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 3, rules);
}

void InstallZharPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallZhar(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

PartReg r20(20, "open", InstallOpenPart);
PartReg r30(30, "stats", InstallStatsPart);
PartReg r170(170, "mapbossai", InstallMapbossaiPart);
PartReg r180(180, "zhar", InstallZharPart);
CountersReg c20(20, MapOpenCounters);
CountersReg c30(30, MapStatsCounters);
CountersReg c130(130, UniqueMapsCounters);
CountersReg c140(140, ZharCounters);
CountersReg c150(150, MapBossCounters);
CountersReg c210(210, MapBossAiCounters);

}  // namespace
