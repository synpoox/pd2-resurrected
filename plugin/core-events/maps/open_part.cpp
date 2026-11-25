// Native maps (open part): the parts and status counters this lane installs, registered by order (parts.h; #943).
// New parts register here or in a sibling file of this folder and never edit plugin.cpp. Orders are one global sequence.
#include "part_registry.h"

#include "map_open.h"

#include <cstdio>

namespace {

void InstallOpenPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallMapOpen(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, MapOpenCount(), rules);
}

PartReg r20(20, "open", InstallOpenPart);
CountersReg c20(20, MapOpenCounters);

}  // namespace
