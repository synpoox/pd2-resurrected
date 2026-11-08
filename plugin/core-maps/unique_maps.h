// The unique maps' boss deaths: portals and drops (unique_maps.cpp).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Called on every monster death (key_drops.cpp's listener); acts only on a unique map boss in its own map.
auto UniqueMapDeath(d2r::Game* game, d2r::Unit* monster) noexcept -> void;
// PD2 0x1038bfc0 in the Outer Void, from the preset spawn hook (map_stats.cpp): a placeholder monster spawns as a
// random boss through `spawn` (true: the preset is done), a placeholder object gets a linked red portal (false).
auto OuterVoidPreset(d2r::Game* game, void* room, uint8_t* preset,
                     void (*spawn)(d2r::Game*, void*, const uint8_t*) noexcept) noexcept -> bool;
// PD2 0x1038bfc0 in the dungeons (map event 9), from the preset spawn hook (map_stats.cpp): the boss preset spawns as
// the dungeon's boss and the blocker presets as its barriers, through `spawn` (true: the preset is done).
auto DungeonPreset(d2r::Game* game, void* room, uint8_t* preset,
                   void (*spawn)(d2r::Game*, void*, const uint8_t*) noexcept) noexcept -> bool;
// From the room-release check (map_open.cpp): true for a closed dungeon barrier or a live dungeon boss, whose room is
// kept so the boss's death finds the barriers it opens.
auto DungeonKeepsRoom(d2r::Unit* unit) noexcept -> bool;
// From the portal operate (map_open.cpp): an Outer Void portal linked to a spot moves the player there (true).
auto OuterVoidWarp(d2r::Game* game, d2r::Unit* player, d2r::Unit* portal) noexcept -> bool;
// Takes the object init slot of the dungeon barriers (InitFn 30). Returns false when the slot is not D2R's.
auto InstallDungeonBarriers(const D2RL::PluginContext* context) noexcept -> bool;
// From the item spawn hook (map_stats.cpp), on a TC drop's request: Zhar's charm's forced prefix and item level.
auto UniqueMapDropOverride(uint8_t* drop) noexcept -> void;
// Registers the table listener that reads the random uniques' bases. Returns false when a service is missing.
auto InstallUniqueMaps(const D2RL::PluginContext* context) noexcept -> bool;
// Live counters for the status command.
auto UniqueMapsCounters(char* out, size_t size) noexcept -> void;
