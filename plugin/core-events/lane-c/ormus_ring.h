// Ormus' Gidbinn ring is corrupted, as PD2's (ormus_ring.cpp, #1950).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks the Gidbinn ring grant, found by signature; `report` gets one entry. Returns 1 when it is on.
auto InstallOrmusRing(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto OrmusRingCounters(char* out, size_t size) noexcept -> void;
