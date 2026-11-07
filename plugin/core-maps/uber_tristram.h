// The Uber Tristram primes' drop (uber_tristram.cpp).
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Turns D2R's own primes' drop off (MutationService); until it succeeds TristramDeath drops nothing.
auto InstallTristram(const D2RL::PluginContext* context, char* report, size_t size) noexcept -> void;
// Called on every monster death (key_drops.cpp's listener); acts on Uber Mephisto, Diablo and Baal.
auto TristramDeath(d2r::Game* game, d2r::Unit* monster) noexcept -> void;
// Live counters for the status command.
auto TristramCounters(char* out, size_t size) noexcept -> void;
