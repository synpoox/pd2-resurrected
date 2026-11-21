// Stash affinity (#1553): a map, bossing or map-material item quick-moved from the inventory into the stash goes to its
// reserved shared stash page and cell (common/stash_home.h), and the Maps tab's selected base's maps are swapped onto
// the Maps page.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace stash_affinity {

// Starts the game-thread watch with the first game. Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// The routing counters, for the status command.
void Counters(char* out, size_t size) noexcept;

// Any thread: the map base the Maps tab shows (its item code; 0 for none). The game thread moves the last base's maps
// back to their tier's page and this one's onto the Maps page.
void ShowBase(uint32_t code) noexcept;

// Any thread: whether the Maps tab is on screen, so the game thread counts the maps per base.
void SetMapsOpen(bool open) noexcept;

// Any thread: the maps of a tier's base (tier 0-2, its place in the tier's list) on the reserved pages, as last counted.
auto MapCount(int32_t tier, int32_t index) noexcept -> uint32_t;

}  // namespace stash_affinity
