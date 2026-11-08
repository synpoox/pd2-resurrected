// Zhar's Sanctum's seals: the portals and tomes PD2 places in levels 176-179 and the tome's operate (zhar.cpp).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// PD2 0x1038bfc0 in Zhar's Sanctum, from the preset spawn hook (map_stats.cpp): a placeholder object gets the seal
// portals or a tome in its place. True when it took the preset, so the placeholder itself does not spawn.
auto ZharPreset(d2r::Game* game, void* room, const uint8_t* preset) noexcept -> bool;
// Installs PD2's operate 80 (the seal portals and tomes) in D2R's free operate slot 64; `report` gets one entry.
// Returns 1 when installed.
auto InstallZhar(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto ZharCounters(char* out, size_t size) noexcept -> void;
