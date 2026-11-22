// PD2 skill functions in free srvdofunc/cltdofunc/srvstfunc slots, and the install of those and the missile functions
// (skill_functions.cpp, missile_functions.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Writes every function into its slot; `report` gets one entry per slot. Returns how many were installed.
auto InstallSkillFunctions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto SkillFunctionCount() noexcept -> int;
// Live counters for the status command.
auto SkillFunctionCounters(char* out, size_t size) noexcept -> void;

namespace d2r {
struct Game;
struct Unit;
}  // namespace d2r
// PD2's splash and skills on striking on a Whirlwind or Blade Dance hit, which D2R's skill-on-hit refuses (#1812,
// #1889): item_rules' HitEvent calls it after D2R's own event dispatch.
auto SpinSplashes(d2r::Game* game, int32_t event, d2r::Unit* unit, d2r::Unit* target, const uint8_t* damage) noexcept
	-> void;
