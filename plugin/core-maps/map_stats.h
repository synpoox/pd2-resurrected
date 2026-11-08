// Map stats on monsters and players (map_stats.cpp).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Routes an opened map's stats into this game's monster and player lists and onto the monster region of its
// level. Call before the map item is freed.
auto RouteMapStats(d2r::Game* game, d2r::Unit* item, int32_t level) noexcept -> void;
// Applies the open map's area stats to one more level (PD2's 0x1038b4d0 with first = 0), as Horazon's portal does.
auto ApplyMapArea(d2r::Game* game, int32_t level) noexcept -> void;
// Installs the monster spawn, player frame, area level, item drop and preset spawn hooks; `report` gets one entry. Returns how
// many were installed.
auto InstallMapStats(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto MapStatsCount() noexcept -> int;
// The open map's area slot (PD2's `Divide - 2000`) for a unit in a map area; 0 elsewhere or for none.
auto MapAreaSlot(d2r::Game* game, d2r::Unit* unit, int slot) noexcept -> int32_t;
// PD2's roll multiplier for a dying monster: 2 when it has `map_glob_skirmish_mode` (fortify) and is no map boss, else 1.
auto MapRollMultiplier(d2r::Unit* monster) noexcept -> int32_t;
// PD2's map-area death drops (0x103799d0's map block): the extra drops and the map-stat drops. Call on a monster's death.
auto MapDeathDrops(d2r::Game* game, d2r::Unit* monster, d2r::Unit* killer, uint32_t* seed) noexcept -> void;
// PD2's Desecration roll (0x103649d0, flag 1): one of the item's corruption rows from a roll of 851-999, its
// corrupt/corruptnum mods as desecrate/desecratenum. Logs the row and roll; false when no row matches.
auto DesecrateItem(d2r::Game* game, d2r::Unit* item) noexcept -> bool;
// Live counters for the status command.
auto MapStatsCounters(char* out, size_t size) noexcept -> void;
