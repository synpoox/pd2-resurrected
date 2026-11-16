// Lane A native maps: the parts and status counters this lane installs, registered by order (parts.h; #943).
// A lane adds its parts here, in its own folder, and never edits plugin.cpp. Orders are one global sequence.
#include "parts.h"

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
