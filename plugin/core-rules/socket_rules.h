// Socketed gems and runes apply their gems.txt mods once (#450), socket_rules.cpp.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

namespace d2r {
struct Unit;
}

// Hooks the socket mods function; `report` gets one entry. Returns how many rules were installed.
auto InstallSocketRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto SocketRuleCount() noexcept -> int;
// Live counters for the status command.
auto SocketRuleCounters(char* out, size_t size) noexcept -> void;
// Marks a gem or rune compact, as the hook does after it is socketed. Returns true when the flag was newly set.
auto MarkSocketedCompact(d2r::Unit* filler) noexcept -> bool;
