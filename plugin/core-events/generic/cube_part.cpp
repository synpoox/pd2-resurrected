// Generic rules (cube part): the parts and status counters this lane installs, registered by order (parts.h; #943).
// New parts register here or in a sibling file of this folder and never edit plugin.cpp. Orders are one global sequence.
#include "part_registry.h"

#include "cube_ops.h"

#include <cstdio>

namespace {

void InstallCubePart(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	char rules[200] = "";
	const int installed = InstallCubeOps(context, rules, sizeof(rules));
	std::snprintf(report, size, "%d/%d installed: %s", installed, CubeOpCount(), rules);
}

PartReg r10(10, "cube", InstallCubePart);
CountersReg c10(10, CubeOpCounters);

}  // namespace
