// The parts registry (#943): each lane registers its parts and status counters from its own folder (lane-a .. lane-d,
// parts_shared.cpp) with a PartReg / CountersReg object; plugin.cpp installs them sorted by order, which is the install
// sequence the log shows. Orders are one global sequence (docs/core-events-lanes.md): a new part takes an unused number.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

using InstallFn = void (*)(const D2RL::PluginContext* context, char* report, size_t size) noexcept;
using CountersFn = void (*)(char* out, size_t size) noexcept;

struct Part {
	const char* word;
	InstallFn install;
	char report[300];
	int order;
};

// A part: `word` is its one-word name for the log, the status command and core-events-off.txt.
struct PartReg {
	PartReg(int order, const char* word, InstallFn install) noexcept;
};
// A status-command counters line.
struct CountersReg {
	CountersReg(int order, CountersFn fn) noexcept;
};

// The registered parts and counters, sorted by order (stable). `count` gets their number.
auto Parts(size_t* count) noexcept -> Part*;
auto Counters(size_t* count) noexcept -> const CountersFn*;
// True when a registration did not fit (the registry holds 64 of each); plugin.cpp logs it.
auto RegistryOverflowed() noexcept -> bool;
