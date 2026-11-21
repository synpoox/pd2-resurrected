// PD2 "Monsters Remaining" (#1534, #1656): the count of living hostile monsters in the level, as the last zone info row
// (top right, shown with the automap). Counts D2R own per-level ledger (the one its Den of Evil check reads), so pets, summons, mercenaries
// and NPCs are never in it. Host and single player only: a TCP/IP client runs no server here, so it shows nothing.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

namespace monsters_remaining {

// Hooks the ledger spawn call (to learn the game region array) and D2R's zone info builder (the line is its last row).
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// The level, its ledger and what is shown, for the status command.
void Counters(char* out, size_t size) noexcept;

}  // namespace monsters_remaining