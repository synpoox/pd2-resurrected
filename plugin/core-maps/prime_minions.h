// The Uber Tristram primes summon their minions (prime_minions.cpp).
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Writes the three AI table slots; `report` gets one entry. Returns 1 when all three are in.
auto InstallPrimeMinions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto PrimeMinionsCounters(char* out, size_t size) noexcept -> void;
