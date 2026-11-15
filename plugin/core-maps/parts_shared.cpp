// Lane S shared: the parts and status counters this lane installs, registered by order (parts.h; #943).
// A lane adds its parts here, in its own folder, and never edits plugin.cpp. Orders are one global sequence.
#include "entity_pool.h"
#include "parts.h"

#include "key_drops.h"

#include <cstdio>

namespace {

void InstallPoolPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	(void)context;
	InstallEntityPool(report, size);
}

void InstallKeysPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallKeyDrops(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, 1, rules);
}

PartReg r50(50, "pool", InstallPoolPart);
PartReg r60(60, "keys", InstallKeysPart);
CountersReg c50(50, KeyDropsCounters);

}  // namespace
