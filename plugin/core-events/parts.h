// The parts registry (#943): each lane registers its parts and status counters from its own folder (lane-a .. lane-d,
// parts_shared.cpp) with a PartReg / CountersReg object; plugin.cpp installs them sorted by order, which is the install
// sequence the log shows. Orders are one global sequence (docs/core-events-lanes.md): a new part takes an unused number.
#pragma once

#include "sig.h"

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <span>

using InstallFn = void (*)(const D2RL::PluginContext* context, char* report, size_t size) noexcept;
using CountersFn = void (*)(char* out, size_t size) noexcept;

struct Part {
	const char* word;
	InstallFn install;
	char report[300];
	int order;
	// The signature entries (d2r::sig) it reads. Empty: a part that reads fixed d2r::rva values, installed on the
	// build-93847 image only. Listed: installed on any image where they all resolved (plugin-common's README,
	// "Engine addresses by signature"). A part lists them only when it reads no fixed rva value.
	std::span<sigscan::Entry* const> uses {};
};

// A part: `word` is its one-word name for the log, the status command and core-events-off.txt.
struct PartReg {
	PartReg(int order, const char* word, InstallFn install, std::span<sigscan::Entry* const> uses = {}) noexcept;
};
// Whether a part with `uses` is left out (its off word, or an entry in `uses` did not resolve on this image), writing
// the reason into `out`. Parts without `uses` are gated by the build check in plugin.cpp, not here.
auto UsesSkipReason(const Part& part, const char* pluginName, char* out, size_t size) noexcept -> bool;
// A status-command counters line.
struct CountersReg {
	CountersReg(int order, CountersFn fn) noexcept;
};

// The registered parts and counters, sorted by order (stable). `count` gets their number.
auto Parts(size_t* count) noexcept -> Part*;
auto Counters(size_t* count) noexcept -> const CountersFn*;
// True when a registration did not fit (the registry holds 64 of each); plugin.cpp logs it.
auto RegistryOverflowed() noexcept -> bool;
