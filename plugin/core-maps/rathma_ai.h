// PD2's Summoner AI for Rathma, Mendeln and their clones (rathma_ai.cpp).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Swaps monai row 53's think for the port's; `report` gets one entry. Returns 1 when it is on.
auto InstallRathmaAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Kills (no drop) every live void knight, golem and totem of the fight; returns how many.
auto KillRathmaAdds(d2r::Game* game) noexcept -> int;
// Stage 5 -> 6: the clone that did not die (`dead` is the other) takes RathmaDeath's buff; true when it did.
auto BuffRathmaSurvivor(d2r::Game* game, d2r::Unit* dead) noexcept -> bool;
// Live counters for the status command.
auto RathmaAiCounters(char* out, size_t size) noexcept -> void;
