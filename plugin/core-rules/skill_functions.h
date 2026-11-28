// PD2 skill functions in free srvdofunc/cltdofunc/srvstfunc slots, and the install of those and the missile functions
// (skill_functions.cpp, missile_functions.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Installs every PD2R_SLOT and skill/ rule and keeps each result (install_report.h); `report` gets the one-line
// installed/expected summary. Logs a WARN per partial and an ERROR per failed install. Returns how many were installed.
auto InstallSkillFunctions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// `core-rules installs [failed]`: the summary, then every install's result (or only the partial and failed ones).
auto WriteInstallResults(const D2RL::ConsoleCommandContext* command, bool failedOnly) noexcept -> void;
auto SkillFunctionCount() noexcept -> int;
// Live counters for the status command.
auto SkillFunctionCounters(char* out, size_t size) noexcept -> void;

// Mercenary monster classes (monstats ids; rows roguehire, act2hire, act3hire, act5hire1, act5hire2, act4hire,
// act4hireLight): stands in for PD2's hireling test (1.13c's D2Common #11104). act4hire (the Dark Ascendant) and
// act4hireLight (the Light Ascendant, #1953) are not in that list; treated as hirelings like the others.
inline constexpr int32_t HirelingClasses[] = {271, 338, 359, 560, 561, 1073, 1322};

namespace d2r {
struct Game;
struct Unit;
}  // namespace d2r
// PD2's splash and skills on striking on a Whirlwind or Blade Dance hit, which D2R's skill-on-hit refuses (#1812,
// #1889): item_rules' HitEvent calls it after D2R's own event dispatch.
auto SpinSplashes(d2r::Game* game, int32_t event, d2r::Unit* unit, d2r::Unit* target, const uint8_t* damage) noexcept
	-> void;
// D2R's AI owner getter (93847: 0x4a53c0, (monster) -> its owner unit or null), as the Blade Sentinel install read it
// from the think's call sites; null before that install or when it missed. The stun refresh rule (#1947) reads it too.
using AiOwnerFn = d2r::Unit* (*)(d2r::Unit* monster) noexcept;
auto AiOwnerGetter() noexcept -> AiOwnerFn;
