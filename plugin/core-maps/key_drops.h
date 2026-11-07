// PD2's key drop rule on every monster death (key_drops.cpp).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Registers the monster-death listener; `report` gets one entry. Returns 1 when it is on.
auto InstallKeyDrops(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// PD2's 0x10381510: one of the eight unique maps, dropped by the monster (the map-area roll and Horazon's 10%).
auto DropUniqueMap(d2r::Game* game, d2r::Unit* monster) noexcept -> void;
// Live counters for the status command: deaths seen and each roll's hits.
auto KeyDropsCounters(char* out, size_t size) noexcept -> void;
