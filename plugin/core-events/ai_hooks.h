// PD2's boss helpers around the toolkit's AI registry (plugin-common ai_registry.h, which holds the think registry and its
// dispatch: ai_registry::RegisterAt and ai_registry::Vanilla): the test aid for a boss's life, the idle window and the
// state half of the boss immunity.
#pragma once

#include "ai_registry.h"
#include "pd2_engine.h"

#include <cstdint>

// A game-check aid (`core-events life <percent> <class>`): the next think of a ported boss of merged monstats class
// `classId` first sets its life to `percent` of its max, for the life events of a boss that blinks away from clicks.
// One request, used once; dropped if no such boss thinks within 10 seconds.
auto RequestBossLife(int32_t percent, int32_t classId) noexcept -> void;
// PD2's idle window for a boss left alone (game+0x26fe; docs/pd2/uber-bosses-dll.md, section 5): the
// room-release hook (map_open.cpp's KeepBossRoom, PD2 0x102fc6c0) marks a boss whose room has had no player for 10+
// ticks; the boss's next think starts the game's 15-tick window when none runs.
auto MarkBossAlone(d2r::Unit* boss) noexcept -> void;
// True while the game's window runs. A boss passes itself (to start the window it was marked for) and whether a player
// is in its level (the window counts down only then); the fight's other AIs pass null and false, and only read it.
auto IdleWindow(d2r::Game* game, d2r::Unit* boss, bool playerInLevel) noexcept -> bool;

// The state half of PD2's boss immunity (0x10380770): strips up to 20 stat lists each of poison (2) and open wounds
// (62) on every immune call, and sets or clears state 8 (resistall) only when the caller asks (PD2's third argument:
// Lucion's idle window pushes 1, his portal phase 0). Returns the number of lists stripped.
auto ImmunityStates(d2r::Game* game, d2r::Unit* unit, bool on, bool setState) noexcept -> int;
