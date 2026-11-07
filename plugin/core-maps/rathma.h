// The Rathma fight's stages outside the AI: the spawns as each level's rooms activate, the deaths and the drops
// (rathma.cpp).
#pragma once

#include "d2r.h"

#include <cstddef>

// Called on every monster death (key_drops.cpp's listener); acts only on Rathma, Mendeln and their clones.
auto RathmaDeath(d2r::Game* game, d2r::Unit* monster) noexcept -> void;
// Called as a room activates, before its monsters spawn (map_events.cpp's hook): the stage's spawns on the level's
// first room. True when the room is in the stage's arena, which spawns none of its own monsters.
auto RathmaRoom(d2r::Game* game, void* room, int32_t level) noexcept -> bool;
// Called on taking a portal whose destination is the player's own level (map_open.cpp's operate): the stage's portal
// into the arena. True when the player was moved there.
auto RathmaArenaPortal(d2r::Game* game, d2r::Unit* player, int32_t level) noexcept -> bool;
// Live counters for the status command.
auto RathmaCounters(char* out, size_t size) noexcept -> void;
