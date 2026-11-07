// Opening a map: item spell 16 (map_open.cpp).
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// A portal (objects `row`) at (x, y) in `room` to `level`, made as the portal maker 0x432ce0 makes one but with no
// return portal; it opens after the row's open frames. Null when none was made.
auto SpawnPortal(d2r::Game* game, void* room, int32_t row, int32_t x, int32_t y, int32_t level) noexcept -> d2r::Unit*;

// Installs the item spell check and use hooks; `report` gets one entry. Returns how many rules were installed.
auto InstallMapOpen(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto MapOpenCount() noexcept -> int;
// Live counters for the status command.
auto MapOpenCounters(char* out, size_t size) noexcept -> void;
