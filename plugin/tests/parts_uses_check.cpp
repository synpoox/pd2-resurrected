// Host self-check of core-events' parts with `uses` (#1081): a part installs when every signature entry it lists
// resolved, and stays off with the entry's name otherwise. No game. Parts without `uses` are gated in plugin.cpp.
//   cmake -S plugin -B plugin/build-checks -A x64 && cmake --build plugin/build-checks --config Release --target parts_uses_check
#include "parts.h"

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;
int g_installs = 0;

void Check(bool ok, const char* what) {
	std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	g_failures += ok ? 0 : 1;
}

void InstallProbe(const D2RL::PluginContext*, char* report, size_t size) noexcept {
	++g_installs;
	std::snprintf(report, size, "on");
}

sigscan::Entry g_resolved {"ProbeResolved", "test", sigscan::Kind::Code, 0, 0, ""};
sigscan::Entry g_missing {"ProbeMissing", "test", sigscan::Kind::Code, 0, 0, ""};
sigscan::Entry* const ResolvedUses[] = {&g_resolved};
sigscan::Entry* const MixedUses[] = {&g_resolved, &g_missing};

PartReg g_probe(900, "usesprobe", InstallProbe, ResolvedUses);

}  // namespace

int main() {
	size_t count = 0;
	Part* parts = Parts(&count);
	Check(count == 1 && std::strcmp(parts[0].word, "usesprobe") == 0, "the registration keeps its word");
	Check(count == 1 && parts[0].uses.size() == 1 && parts[0].uses[0] == &g_resolved, "the registration keeps its uses");

	g_resolved.state = sigscan::State::Ok;
	g_missing.state = sigscan::State::NotFound;

	Part& p = parts[0];
	Check(!UsesSkipReason(p, "core-events", p.report, sizeof(p.report)), "a part whose entries resolved is not skipped");
	if (!UsesSkipReason(p, "core-events", p.report, sizeof(p.report))) {
		p.install(nullptr, p.report, sizeof(p.report));
	}
	Check(g_installs == 1 && std::strcmp(p.report, "on") == 0, "the install runs once for it");

	Part mixed {"usesmiss", InstallProbe, {}, 901, MixedUses};
	Check(UsesSkipReason(mixed, "core-events", mixed.report, sizeof(mixed.report)), "one unresolved entry skips the part");
	Check(std::strstr(mixed.report, "ProbeMissing") != nullptr && std::strncmp(mixed.report, "off:", 4) == 0,
	      "the skip report names the entry");
	Check(g_installs == 1, "a skipped part is not installed");

	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
