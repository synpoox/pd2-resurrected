// Test commands that act on any monster (`core-events kill <class>|unit <id>`, `life <pct> unit <id>`): see test_kill.cpp.
#pragma once

#include "pd2_engine.h"
#include "test_cmd.h"

#include <D2RLPlugin/api.h>

// Queues `cmd` on the game thread; false (and why, in `why`) when it cannot be queued
// The first live game (the local player's, single player), or null. Shared with test_item.cpp.
auto FirstGame() noexcept -> d2r::Game*;
// `corrupt` / `desecrate` (test_item.cpp): hops UI thread -> game thread, acts on the cursor item or inventory (0,0).
auto QueueItemCommand(const D2RL::PluginContext* context, pd2::test_cmd::Cmd::Kind kind, char* why, size_t whySize) noexcept -> bool;
auto QueueTestCommand(const D2RL::PluginContext* context, const pd2::test_cmd::Cmd& cmd, char* why, size_t whySize) noexcept -> bool;
