// Lane B generic rules: the parts and status counters this lane installs, registered by order (parts.h; #943).
// A lane adds its parts here, in its own folder, and never edits plugin.cpp. Orders are one global sequence.
#include "parts.h"

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
