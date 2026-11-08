// The Rathma fight's stages outside the AI: the spawns as each level's rooms activate, the deaths and the drops
// (rathma.cpp).
#pragma once

#include "pd2_engine.h"

#include <cstddef>

// Called on every monster death (key_drops.cpp's listener); acts only on Rathma, Mendeln and their clones.
auto RathmaDeath(d2r::Game* game, d2r::Unit* monster) noexcept -> void;
// Called as a room activates, before its monsters spawn (map_events.cpp's hook): the stage's spawns on the level's
// first room. True when the room is in the stage's arena, which spawns none of its own monsters.
auto RathmaRoom(d2r::Game* game, void* room, int32_t level) noexcept -> bool;
// Called on taking a portal whose destination is the player's own level (map_open.cpp's operate): the stage's portal
// into the arena. True when the player was moved there.
auto RathmaArenaPortal(d2r::Game* game, d2r::Unit* player, int32_t level) noexcept -> bool;
// PD2 0x103a9780, tier 0: a portal taken from outside Harrogath into a Rathma level moves the exit of the grey
// re-entry portals (PD2 relinks them to a new `empty` marker, 0x1038a510): the arena portal in stages 1, 3 and 5+ to
// the stage's spot, a portal to the next level to that level's arrival. Called before the player moves.
auto RathmaMoveReentry(d2r::Game* game, d2r::Unit* player, int32_t from, int32_t to) noexcept -> void;
// The grey portals' exit for a grey portal into `level`: its level, written to `level`, and spot. False while it was
// never moved, or `level` is no Rathma level.
auto RathmaReentry(d2r::Game* game, int32_t& level, int32_t xy[2]) noexcept -> bool;
// Places a player just warped into the exit's level at its spot; an arrival exit leaves the warp's placement.
auto RathmaPlaceAt(d2r::Game* game, d2r::Unit* player, const int32_t xy[2]) noexcept -> void;
// Live counters for the status command.
auto RathmaCounters(char* out, size_t size) noexcept -> void;
