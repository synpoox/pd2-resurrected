// Named spots of Cartographer's fixed arenas (#958): cartographer.arena.v1 (d2r-tools docs/cartographer/arena-service.md)
// is acquired at first use, never at plugin load (the load order is not defined). No service, or no spot, means the
// caller refuses its fight: nothing is invented.
#pragma once

#include "placements.h"

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Keeps the plugin context the service is acquired through. Acquires nothing; writes one report line.
auto InstallArenaSpots(const D2RL::PluginContext* context, char* report, size_t size) noexcept -> int;
// True when Cartographer builds `level` from its arena plan (isArena).
auto ArenaReady(int32_t level) noexcept -> bool;
// The named spot of the arena on the level bound to `encounter`, in subtiles. False, `out` untouched, when there is none.
auto ArenaSpotOf(const char* encounter, const char* spot, placements::Spot* out) noexcept -> bool;
