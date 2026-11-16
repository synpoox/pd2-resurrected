// Test commands that act on any monster (`core-events kill <class>|unit <id>`, `life <pct> unit <id>`): see test_kill.cpp.
#pragma once

#include "test_cmd.h"

#include <D2RLPlugin/api.h>

// Queues `cmd` on the game thread; false (and why, in `why`) when it cannot be queued
auto QueueTestCommand(const D2RL::PluginContext* context, const pd2::test_cmd::Cmd& cmd, char* why, size_t whySize) noexcept -> bool;
