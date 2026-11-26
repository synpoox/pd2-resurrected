// PD2's inventory hotkeys, configurable (#1567).
#pragma once

#include <D2RLPlugin/api.h>

namespace hotkeys {

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// True when the stash bind is on and is Shift + right click (the stack tabs' take-out follows it, #2180).
auto StashBindIsShiftRight() noexcept -> bool;
}
