// The parts registry (#943; the part type and its install and status code are plugin-common's parts.h): each feature folder registers its parts and status counters (maps/, ubers/, generic/,
// parts_shared.cpp) with a PartReg / CountersReg object; plugin.cpp installs them sorted by order, which is the install
// sequence the log shows. Orders are one global sequence: a new part takes an unused number.
#pragma once

#include "parts.h"  // plugin-common: parts::Part, InstallAll, WriteStatus

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <span>

using CountersFn = void (*)(char* out, size_t size) noexcept;

// A part: `word` is its one-word name for the log, the status command and core-events-off.txt.
struct PartReg {
	PartReg(int order, const char* word, parts::InstallFn install, std::span<sigscan::Entry* const> uses = {}) noexcept;
};
// A status-command counters line.
struct CountersReg {
	CountersReg(int order, CountersFn fn) noexcept;
};

// The registered parts and counters, sorted by order (stable). `count` gets their number.
auto Parts(size_t* count) noexcept -> parts::Part*;
auto Counters(size_t* count) noexcept -> const CountersFn*;
// True when a registration did not fit (the registry holds 64 of each); plugin.cpp logs it.
auto RegistryOverflowed() noexcept -> bool;
