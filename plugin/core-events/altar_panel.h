// The Sacrifice Altar's panel (#1192, altar_panel.cpp): a left panel with one amulet slot and confirm / cancel buttons.
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Part install: the player page, the panel, the buttons' listener and the return watcher. `report` gets one entry.
auto InstallAltarPanel(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Game thread, from the altar's operate: opens the panel for the player that used it.
auto OpenAltarPanel(d2r::Game* game, d2r::Unit* player) noexcept -> void;
// Status-command counters.
auto AltarCounters(char* out, size_t size) noexcept -> void;
