// Shared surface of core-rules' per-rule skill files (plugin/core-rules/skill/*.cpp): each file's install, called
// once from InstallSkillFunctions (skill_functions.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Monster curse cast (#2003), monster_curse.cpp: true when installed; appends its part to `report`.
auto InstallMonsterCurse(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;
