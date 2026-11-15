// Lane S shared: the parts and status counters this lane installs, registered by order (parts.h; #943).
// A lane adds its parts here, in its own folder, and never edits plugin.cpp. Orders are one global sequence.
#include "entity_pool.h"
#include "parts.h"

#include "key_drops.h"
#include "placements.h"

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

// The placement registry (#947): counts what the encounter files registered; a duplicate encounter or id is a defect
void InstallPlacementsPart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	(void)context;
	const auto& r = placements::Reg_();
	size_t bindings = 0, places = 0;
	for (size_t i = 0; i < r.regs; ++i) {
		bindings += r.bindingCounts[i];
		places += r.placeCounts[i];
	}
	std::snprintf(report, size, "%zu encounters, %zu placements in %zu files%s", bindings, places, r.regs,
	              r.overflowed ? " (REGISTRY FULL)" : "");
}

void PlacementsCounters(char* out, size_t size) noexcept {
	std::snprintf(out, size, "placements: %u lookups found nothing", placements::Reg_().missing);
}

PartReg r55(55, "placements", InstallPlacementsPart);
PartReg r50(50, "pool", InstallPoolPart);
PartReg r60(60, "keys", InstallKeysPart);
CountersReg c50(50, KeyDropsCounters);
CountersReg c55(55, PlacementsCounters);

}  // namespace
