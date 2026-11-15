// Lane C map events: the parts and status counters this lane installs, registered by order (parts.h; #943).
// A lane adds its parts here, in its own folder, and never edits plugin.cpp. Orders are one global sequence.
#include "parts.h"

#include "guardian_ai.h"
#include "horazon.h"
#include "map_events.h"

#include <cstdio>

namespace {

void InstallMapeventsPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallMapEvents(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

void InstallEventsPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallEventPortals(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

void InstallGuardianaiPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallGuardianAi(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

PartReg r40(40, "mapevents", InstallMapeventsPart);
PartReg r110(110, "events", InstallEventsPart);
PartReg r160(160, "guardianai", InstallGuardianaiPart);
CountersReg c40(40, MapEventsCounters);
CountersReg c120(120, EventPortalsCounters);
CountersReg c200(200, GuardianAiCounters);

}  // namespace
