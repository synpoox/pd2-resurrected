// Stash affinity (#1553): a map, bossing or map-material item quick-moved from the inventory into the stash goes to its
// reserved shared stash page (common/stash_home.h).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

namespace stash_affinity {

// Starts the game-thread watch with the first game. Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// The routing counters, for the status command.
void Counters(char* out, size_t size) noexcept;

}  // namespace stash_affinity
